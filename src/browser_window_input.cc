// Region-bound trusted pointer and key input over IPC (see docs/ipc.md).
//
// input-region mints a short-lived, one-shot "ph1_" capability for one exact
// tab and frame at one viewport point. The browser itself resolves what is
// under that point through its DevTools session (not page script), checks that
// it belongs to the requested frame, and records the element's box and the
// frame's box. pointer-gesture and key-input consume that capability and send
// Input.dispatchMouseEvent / Input.dispatchKeyEvent, which Blink receives as
// ordinary trusted input, re-checking the hit target before every step.
//
// DevTools sees through same-process frames but stops at a cross-process
// (out-of-process) iframe: there the check proves the point is inside that
// iframe's visible box and uncovered by its parent, not which element inside
// the iframe receives it.

#include "browser_window.h"
#include "browser_window_internal.h"

#include <sys/random.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "include/base/cef_callback.h"
#include "include/base/cef_logging.h"
#include "include/cef_browser.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/internal/vimbrowser_private_api.h"
#include "include/wrapper/cef_closure_task.h"

namespace vimbrowser {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kHandleTtlMs = 15000;
constexpr size_t kMaxHandles = 256;
constexpr size_t kMaxSteps = 256;
constexpr int kMaxStepDelayMs = 5000;
constexpr int kMaxGestureMs = 15000;
constexpr int kMaxKeyUnits = 256;
constexpr int kKeyIntervalMs = 35;
constexpr int kDevToolsTimeoutMs = 4000;
constexpr double kMovedTolerance = 1.5;

struct Rect {
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool empty() const { return x1 <= x0 || y1 <= y0; }
  bool contains(double x, double y) const {
    return x >= x0 && x < x1 && y >= y0 && y < y1;
  }
  Rect intersect(const Rect& o) const {
    return {std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1),
            std::min(y1, o.y1)};
  }
  bool near(const Rect& o) const {
    return std::fabs(x0 - o.x0) <= kMovedTolerance &&
           std::fabs(y0 - o.y0) <= kMovedTolerance &&
           std::fabs(x1 - o.x1) <= kMovedTolerance &&
           std::fabs(y1 - o.y1) <= kMovedTolerance;
  }
};

std::string RectJson(const Rect& r) {
  std::ostringstream out;
  out << "{\"x\":" << r.x0 << ",\"y\":" << r.y0 << ",\"w\":" << (r.x1 - r.x0)
      << ",\"h\":" << (r.y1 - r.y0) << "}";
  return out.str();
}

std::string ErrorJson(std::string_view code, std::string_view message,
                      std::string_view extra = {}) {
  std::string out = "{\"ok\":false,\"error\":{\"code\":\"" + JsonEscape(code) +
                    "\",\"message\":\"" + JsonEscape(message) + "\"}";
  if (!extra.empty()) {
    out += ",";
    out += extra;
  }
  out += "}";
  return out;
}

// One frame on the DevTools side of the binding (same-target frames only).
struct BoundFrame {
  std::string devtools_id;   // empty for a cross-process boundary frame
  std::string loader_id;
  int owner_node = 0;        // backendNodeId of the <iframe> in its parent, 0 for main
};

// A minted capability. Everything needed to re-prove the binding later.
struct InputHandle {
  uint64_t tab_id = 0;
  int browser_id = 0;
  std::string frame_id;      // the CEF frame the caller named
  std::string frame_url;
  std::vector<BoundFrame> chain;  // main .. deepest verified frame
  bool cross_process = false;     // deepest verified frame is an OOPIF boundary
  bool below_boundary = false;    // the named frame is nested inside that OOPIF
  std::string boundary_frame;     // DevTools id of that OOPIF's content frame
  double boundary_x = 0, boundary_y = 0;  // its content-box origin (viewport px)
  int node = 0;                   // backendNodeId hit at the minted point
  Rect node_box;                  // its border box when minted (viewport CSS px)
  Rect node_region;               // node box clipped to the frame region
  Rect frame_region;              // the bound frame's visible viewport box
  double mint_x = 0, mint_y = 0;
  Clock::time_point expires;
};

std::map<std::string, InputHandle>& Handles() {
  static std::map<std::string, InputHandle> handles;
  return handles;
}

std::string NewHandleId() {
  // getentropy() is declared by <sys/random.h> on both Linux and Darwin;
  // getrandom() is Linux-only.
  std::array<unsigned char, 16> bytes{};
  while (getentropy(bytes.data(), bytes.size()) != 0) {
  }
  static const char* hex = "0123456789abcdef";
  std::string id = "ph1_";
  for (unsigned char b : bytes) {
    id += hex[b >> 4];
    id += hex[b & 15];
  }
  return id;
}

void PruneHandles() {
  auto& handles = Handles();
  const auto now = Clock::now();
  for (auto it = handles.begin(); it != handles.end();) {
    it = it->second.expires <= now ? handles.erase(it) : std::next(it);
  }
  while (handles.size() >= kMaxHandles) {
    auto oldest = std::min_element(handles.begin(), handles.end(),
        [](const auto& a, const auto& b) { return a.second.expires < b.second.expires; });
    handles.erase(oldest);
  }
}

// Removes the handle before anything else is checked: one handle, one attempt.
std::optional<InputHandle> ConsumeHandle(const std::string& id, std::string* code) {
  auto& handles = Handles();
  auto it = handles.find(id);
  if (it == handles.end()) {
    *code = "invalid_handle";
    return std::nullopt;
  }
  InputHandle handle = std::move(it->second);
  handles.erase(it);
  if (handle.expires <= Clock::now()) {
    *code = "expired_handle";
    return std::nullopt;
  }
  return handle;
}

int NextDevToolsMessageId() {
  static int next = 910000000;
  if (next >= 919999999) next = 910000000;
  return next++;
}

// Sequential DevTools calls on one tab's browser-owned DevTools session.
class InputDevTools final : public CefDevToolsMessageObserver {
 public:
  using Result = std::function<void(bool, CefRefPtr<CefDictionaryValue>)>;

  explicit InputDevTools(CefRefPtr<CefBrowserHost> host) : host_(host) {
    registration_ = host_->AddDevToolsMessageObserver(this);
  }

  void Call(const std::string& method, CefRefPtr<CefDictionaryValue> params,
            Result done) {
    if (std::getenv("VIMBROWSER_INPUT_TRACE")) {
      const auto start = Clock::now();
      done = [done = std::move(done), method, start](bool ok, CefRefPtr<CefDictionaryValue> r) {
        LOG(ERROR) << "input-trace " << method << " ok=" << ok << " ms="
                   << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        done(ok, r);
      };
    }
    pending_ = std::move(done);
    const int requested = NextDevToolsMessageId();
    message_id_ = requested;
    const int actual = host_->ExecuteDevToolsMethod(requested, method, params);
    if (actual == 0) {
      Resolve(false, nullptr);
      return;
    }
    message_id_ = actual;
    const int id = message_id_;
    CefRefPtr<InputDevTools> self(this);
    CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<InputDevTools> self, int id) {
      if (self->message_id_ == id) self->Resolve(false, nullptr);
    }, self, id), kDevToolsTimeoutMs);
  }

  // Input sent to a cross-process iframe's own DevTools target goes straight
  // to that frame's widget. Through the page target, the browser must first
  // route each event; for a hidden tab that lookup can stall about a second
  // per mouse move. Sessions come from a flattened Target.setAutoAttach.
  void CallSession(const std::string& session, const std::string& method,
                   CefRefPtr<CefDictionaryValue> params, Result done) {
    if (std::getenv("VIMBROWSER_INPUT_TRACE")) {
      const auto start = Clock::now();
      done = [done = std::move(done), method, start](bool ok, CefRefPtr<CefDictionaryValue> r) {
        auto v = CefValue::Create();
        v->SetDictionary(r->Copy(false));
        LOG(ERROR) << "input-trace session " << method << " ok=" << ok << " ms="
                   << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count()
                   << " " << CefWriteJSON(v, JSON_WRITER_DEFAULT).ToString().substr(0, 300);
        done(ok, r);
      };
    }
    pending_ = std::move(done);
    const int id = NextDevToolsMessageId();
    message_id_ = id;
    auto message = CefDictionaryValue::Create();
    message->SetInt("id", id);
    message->SetString("method", method);
    message->SetDictionary("params", params);
    message->SetString("sessionId", session);
    auto value = CefValue::Create();
    value->SetDictionary(message);
    const std::string json = CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
    if (!host_->SendDevToolsMessage(json.data(), json.size())) {
      Resolve(false, nullptr);
      return;
    }
    CefRefPtr<InputDevTools> self(this);
    CefPostDelayedTask(TID_UI, base::BindOnce([](CefRefPtr<InputDevTools> self, int id) {
      if (self->message_id_ == id) self->Resolve(false, nullptr);
    }, self, id), kDevToolsTimeoutMs);
  }

  std::string SessionFor(const std::string& target_id) const {
    auto it = sessions_.find(target_id);
    return it == sessions_.end() ? std::string() : it->second;
  }

  void ExpectAttach() { attaching_ = true; }

  // Results from a child session carry its sessionId and do not reach
  // OnDevToolsMethodResult; pick out ours here.
  bool OnDevToolsMessage(CefRefPtr<CefBrowser>, const void* message, size_t size) override {
    if (!attaching_ || !message_id_ || !message || size > 65536) return false;
    const std::string_view text(static_cast<const char*>(message), size);
    if (text.find("\"sessionId\"") == std::string_view::npos ||
        text.find("\"id\":" + std::to_string(message_id_)) == std::string_view::npos) {
      return false;
    }
    CefRefPtr<CefValue> value = CefParseJSON(message, size, JSON_PARSER_RFC);
    CefRefPtr<CefDictionaryValue> dict =
        value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
    if (!dict || dict->GetType("id") != VTYPE_INT || dict->GetInt("id") != message_id_) return false;
    const bool ok = dict->GetType("result") == VTYPE_DICTIONARY;
    Resolve(ok, ok ? dict->GetDictionary("result")
                   : (dict->GetType("error") == VTYPE_DICTIONARY ? dict->GetDictionary("error")
                                                                 : CefDictionaryValue::Create()));
    return true;
  }

  void OnDevToolsEvent(CefRefPtr<CefBrowser>, const CefString& method, const void* params,
                       size_t params_size) override {
    if (!attaching_ || method.ToString() != "Target.attachedToTarget" || !params ||
        !params_size) {
      return;
    }
    CefRefPtr<CefValue> value = CefParseJSON(params, params_size, JSON_PARSER_RFC);
    if (!value || value->GetType() != VTYPE_DICTIONARY) return;
    CefRefPtr<CefDictionaryValue> p = value->GetDictionary();
    CefRefPtr<CefDictionaryValue> info =
        p->GetType("targetInfo") == VTYPE_DICTIONARY ? p->GetDictionary("targetInfo") : nullptr;
    if (!info || info->GetString("type").ToString() != "iframe") return;
    sessions_[info->GetString("targetId").ToString()] = p->GetString("sessionId").ToString();
  }

  // Detaches every session this operation attached and stops auto-attaching.
  void Close() {
    if (host_ && attaching_) {
      for (const auto& [target, session] : sessions_) {
        auto p = CefDictionaryValue::Create();
        p->SetString("sessionId", session);
        host_->ExecuteDevToolsMethod(0, "Target.detachFromTarget", p);
      }
      auto p = CefDictionaryValue::Create();
      p->SetBool("autoAttach", false);
      p->SetBool("waitForDebuggerOnStart", false);
      p->SetBool("flatten", true);
      host_->ExecuteDevToolsMethod(0, "Target.setAutoAttach", p);
    }
    sessions_.clear();
    attaching_ = false;
    registration_ = nullptr;
  }

  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int message_id, bool success,
                              const void* result, size_t result_size) override {
    if (message_id == 0 || message_id != message_id_) return;
    CefRefPtr<CefDictionaryValue> dict;
    if (result && result_size) {
      CefRefPtr<CefValue> value = CefParseJSON(result, result_size, JSON_PARSER_RFC);
      if (value && value->GetType() == VTYPE_DICTIONARY) dict = value->GetDictionary();
    }
    Resolve(success, dict ? dict : CefDictionaryValue::Create());
  }

 private:
  void Resolve(bool success, CefRefPtr<CefDictionaryValue> dict) {
    message_id_ = 0;
    if (!pending_) return;
    Result done = std::move(pending_);
    pending_ = nullptr;
    done(success, dict ? dict : CefDictionaryValue::Create());
  }

  CefRefPtr<CefBrowserHost> host_;
  CefRefPtr<CefRegistration> registration_;
  Result pending_;
  int message_id_ = 0;
  std::map<std::string, std::string> sessions_;  // iframe targetId -> sessionId
  bool attaching_ = false;

  IMPLEMENT_REFCOUNTING(InputDevTools);
  DISALLOW_COPY_AND_ASSIGN(InputDevTools);
};

CefRefPtr<CefDictionaryValue> Params() { return CefDictionaryValue::Create(); }

std::optional<Rect> QuadBox(CefRefPtr<CefDictionaryValue> model, const char* key) {
  if (!model || model->GetType(key) != VTYPE_LIST) return std::nullopt;
  CefRefPtr<CefListValue> quad = model->GetList(key);
  if (!quad || quad->GetSize() != 8) return std::nullopt;
  Rect r{1e18, 1e18, -1e18, -1e18};
  auto at = [&quad](size_t i) {
    return quad->GetType(i) == VTYPE_INT ? static_cast<double>(quad->GetInt(i)) : quad->GetDouble(i);
  };
  for (size_t i = 0; i < 8; i += 2) {
    const double x = at(i), y = at(i + 1);
    r.x0 = std::min(r.x0, x); r.x1 = std::max(r.x1, x);
    r.y0 = std::min(r.y0, y); r.y1 = std::max(r.y1, y);
  }
  return r;
}

struct DevFrame {
  std::string id, parent, url, loader;
};

void CollectFrames(CefRefPtr<CefDictionaryValue> tree, const std::string& parent,
                   std::map<std::string, DevFrame>* out) {
  if (!tree || tree->GetType("frame") != VTYPE_DICTIONARY) return;
  CefRefPtr<CefDictionaryValue> frame = tree->GetDictionary("frame");
  DevFrame f{frame->GetString("id").ToString(), parent,
             frame->GetString("url").ToString() +
                 (frame->HasKey("urlFragment") ? frame->GetString("urlFragment").ToString() : ""),
             frame->GetString("loaderId").ToString()};
  (*out)[f.id] = f;
  if (tree->GetType("childFrames") == VTYPE_LIST) {
    CefRefPtr<CefListValue> children = tree->GetList("childFrames");
    for (size_t i = 0; i < children->GetSize(); ++i) {
      CollectFrames(children->GetDictionary(i), f.id, out);
    }
  }
}

struct CefFrameInfo {
  CefRefPtr<CefFrame> frame;
  std::string id, parent, url;
  int depth = 0;
  bool oopif = false;
};

std::map<std::string, CefFrameInfo> CefFrames(CefRefPtr<CefBrowser> browser) {
  std::map<std::string, CefFrameInfo> out;
  std::vector<CefString> ids;
  browser->GetFrameIdentifiers(ids);
  for (const CefString& id : ids) {
    CefRefPtr<CefFrame> frame = browser->GetFrameByIdentifier(id);
    if (!frame || !frame->IsValid()) continue;
    CefFrameInfo info;
    info.frame = frame;
    info.id = id.ToString();
    info.url = frame->GetURL().ToString();
    CefRefPtr<CefFrame> parent = frame->GetParent();
    info.parent = parent ? parent->GetIdentifier().ToString() : "";
    for (CefRefPtr<CefFrame> p = parent; p && info.depth < 64; p = p->GetParent()) ++info.depth;
    info.oopif = vimbrowser_frame_is_out_of_process(browser->GetIdentifier(), info.id.data(),
                                                     info.id.size());
    out[info.id] = info;
  }
  return out;
}

std::vector<std::string> CefChain(const std::map<std::string, CefFrameInfo>& frames,
                                  std::string id) {
  std::vector<std::string> chain;
  while (!id.empty() && frames.count(id) && chain.size() < 64) {
    chain.insert(chain.begin(), id);
    id = frames.at(id).parent;
  }
  return chain;
}

// --- payloads ---------------------------------------------------------------

CefRefPtr<CefDictionaryValue> DecodePayload(const std::string& encoded, std::string* error) {
  if (encoded.empty() || encoded.size() > 65536) {
    *error = ErrorJson("invalid_payload", "input payload is missing or too large");
    return nullptr;
  }
  CefRefPtr<CefBinaryValue> decoded = CefBase64Decode(encoded);
  if (!decoded || decoded->GetSize() == 0 || decoded->GetSize() > 49152) {
    *error = ErrorJson("invalid_payload", "input payload is not valid base64");
    return nullptr;
  }
  std::vector<char> bytes(decoded->GetSize());
  decoded->GetData(bytes.data(), bytes.size(), 0);
  CefRefPtr<CefValue> value = CefParseJSON(bytes.data(), bytes.size(), JSON_PARSER_RFC);
  CefRefPtr<CefDictionaryValue> root =
      value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
  if (!root || root->GetType("version") != VTYPE_INT || root->GetInt("version") != 1) {
    *error = ErrorJson("invalid_payload", "input payload has an unsupported schema");
    return nullptr;
  }
  return root;
}

bool Number(CefRefPtr<CefDictionaryValue> d, const char* key, double* out) {
  switch (d->GetType(key)) {
    case VTYPE_INT: *out = d->GetInt(key); return std::isfinite(*out);
    case VTYPE_DOUBLE: *out = d->GetDouble(key); return std::isfinite(*out);
    default: return false;
  }
}

bool HandleArg(CefRefPtr<CefDictionaryValue> root, std::string* handle, std::string* error) {
  *handle = root->GetType("handle") == VTYPE_STRING ? root->GetString("handle").ToString() : "";
  if (!handle->starts_with("ph1_") || handle->size() != 36) {
    *error = ErrorJson("invalid_handle", "input-region handle is malformed");
    return false;
  }
  return true;
}

enum class StepKind { kMove, kDown, kUp };
struct Step {
  StepKind kind;
  double x, y;
  int delay_ms;
};

// --- shared context ---------------------------------------------------------

struct Op {
  uint64_t tab_id = 0;
  CefRefPtr<CefBrowser> browser;
  CefRefPtr<InputDevTools> dt;
  IpcReplyCallback reply;
  bool done = false;

  void Finish(std::string response) {
    if (done) return;
    done = true;
    if (dt) dt->Close();
    dt = nullptr;
    if (reply) {
      IpcReplyCallback r = std::move(reply);
      r(std::move(response));
    }
  }
  void Call(const std::string& method, CefRefPtr<CefDictionaryValue> params,
            InputDevTools::Result next) {
    if (done) return;
    dt->Call(method, params, std::move(next));
  }
};

using OpPtr = std::shared_ptr<Op>;

// Hit-tests a main-viewport point (CSS px). DOM.getNodeForLocation takes
// main-document coordinates, so the current layout scroll offset is added.
void HitTest(const OpPtr& op, double x, double y,
             std::function<void(int node, std::string frame)> next) {
  op->Call("Page.getLayoutMetrics", Params(), [op, x, y, next](bool ok, CefRefPtr<CefDictionaryValue> m) {
    double sx = 0, sy = 0;
    CefRefPtr<CefDictionaryValue> layout =
        ok && m->GetType("cssLayoutViewport") == VTYPE_DICTIONARY ? m->GetDictionary("cssLayoutViewport")
                                                                   : nullptr;
    if (!layout || !Number(layout, "pageX", &sx) || !Number(layout, "pageY", &sy)) {
      next(0, "");
      return;
    }
    auto p = Params();
    p->SetInt("x", static_cast<int>(std::floor(x + sx)));
    p->SetInt("y", static_cast<int>(std::floor(y + sy)));
    p->SetBool("includeUserAgentShadowDOM", false);
    op->Call("DOM.getNodeForLocation", p, [next](bool ok, CefRefPtr<CefDictionaryValue> r) {
      if (!ok || r->GetType("backendNodeId") != VTYPE_INT) {
        next(0, "");
        return;
      }
      next(r->GetInt("backendNodeId"), r->GetString("frameId").ToString());
    });
  });
}

void BorderBox(const OpPtr& op, int node, const char* which,
               std::function<void(std::optional<Rect>)> next) {
  auto p = Params();
  p->SetInt("backendNodeId", node);
  std::string key = which;
  op->Call("DOM.getBoxModel", p, [next, key](bool ok, CefRefPtr<CefDictionaryValue> r) {
    if (!ok || r->GetType("model") != VTYPE_DICTIONARY) {
      next(std::nullopt);
      return;
    }
    next(QuadBox(r->GetDictionary("model"), key.c_str()));
  });
}

// Resolves the boxes of every owner element along the chain and the viewport,
// returning the bound frame's visible region.
void FrameRegion(const OpPtr& op, std::vector<int> owners, Rect acc,
                 std::function<void(std::optional<Rect>)> next) {
  if (owners.empty()) {
    next(acc.empty() ? std::nullopt : std::optional<Rect>(acc));
    return;
  }
  const int owner = owners.front();
  owners.erase(owners.begin());
  BorderBox(op, owner, "content", [op, owners, acc, next](std::optional<Rect> box) {
    if (!box) {
      next(std::nullopt);
      return;
    }
    FrameRegion(op, owners, acc.intersect(*box), next);
  });
}

void Viewport(const OpPtr& op, std::function<void(std::optional<Rect>)> next) {
  op->Call("Page.getLayoutMetrics", Params(), [next](bool ok, CefRefPtr<CefDictionaryValue> r) {
    if (!ok || r->GetType("cssVisualViewport") != VTYPE_DICTIONARY) {
      next(std::nullopt);
      return;
    }
    CefRefPtr<CefDictionaryValue> v = r->GetDictionary("cssVisualViewport");
    double w = 0, h = 0;
    if (!Number(v, "clientWidth", &w) || !Number(v, "clientHeight", &h)) {
      next(std::nullopt);
      return;
    }
    next(Rect{0, 0, w, h});
  });
}

// Re-proves that a handle's frame binding still holds: the named frame exists
// with the same URL, every same-target frame on the chain still has the same
// document (loaderId), and the bound boxes have not moved.
void Revalidate(const OpPtr& op, std::shared_ptr<InputHandle> h,
                std::function<void(std::string code)> next) {
  CefRefPtr<CefFrame> frame = op->browser->GetFrameByIdentifier(h->frame_id);
  if (!frame || !frame->IsValid() || frame->GetURL().ToString() != h->frame_url) {
    next("stale_frame");
    return;
  }
  op->Call("Page.getFrameTree", Params(), [op, h, next](bool ok, CefRefPtr<CefDictionaryValue> r) {
    std::map<std::string, DevFrame> frames;
    if (ok && r->GetType("frameTree") == VTYPE_DICTIONARY) {
      CollectFrames(r->GetDictionary("frameTree"), "", &frames);
    }
    for (const BoundFrame& f : h->chain) {
      if (f.devtools_id.empty()) continue;
      auto it = frames.find(f.devtools_id);
      if (it == frames.end() || it->second.loader != f.loader_id) {
        next("stale_document");
        return;
      }
    }
    BorderBox(op, h->node, "border", [op, h, next](std::optional<Rect> box) {
      if (!box || !box->near(h->node_box)) {
        next("target_moved");
        return;
      }
      next("");
    });
  });
}

// --- input-region -----------------------------------------------------------

// Looks up the <iframe> element of every same-target frame below main.
void ResolveOwners(const OpPtr& op, std::vector<std::string> dchain, size_t i,
                   std::shared_ptr<std::vector<int>> owners, std::shared_ptr<InputHandle> h,
                   std::function<void(std::shared_ptr<std::vector<int>>)> done) {
  if (i >= dchain.size()) {
    done(owners);
    return;
  }
  auto p = Params();
  p->SetString("frameId", dchain[i]);
  op->Call("DOM.getFrameOwner", p, [=](bool ok, CefRefPtr<CefDictionaryValue> r) {
    if (!ok || r->GetType("backendNodeId") != VTYPE_INT) {
      op->Finish(ErrorJson("stale_frame", "frame owner is unavailable"));
      return;
    }
    owners->push_back(r->GetInt("backendNodeId"));
    h->chain[i].owner_node = r->GetInt("backendNodeId");
    ResolveOwners(op, dchain, i + 1, owners, h, done);
  });
}

void MintRegion(const OpPtr& op, std::string frame_id, double x, double y) {
  auto frames = std::make_shared<std::map<std::string, CefFrameInfo>>(CefFrames(op->browser));
  if (!frames->count(frame_id)) {
    op->Finish(ErrorJson("stale_frame", "frame is not active in this tab"));
    return;
  }
  op->Call("Page.getFrameTree", Params(), [=](bool ok, CefRefPtr<CefDictionaryValue> r) {
    auto dev = std::make_shared<std::map<std::string, DevFrame>>();
    if (ok && r->GetType("frameTree") == VTYPE_DICTIONARY) {
      CollectFrames(r->GetDictionary("frameTree"), "", dev.get());
    }
    if (dev->empty()) {
      op->Finish(ErrorJson("document_unavailable", "tab document is unavailable"));
      return;
    }
    HitTest(op, x, y, [=](int node, std::string hit_frame) {
      if (!node || !dev->count(hit_frame)) {
        op->Finish(ErrorJson("target_not_found", "nothing is under that point"));
        return;
      }
      auto p = Params();
      p->SetInt("backendNodeId", node);
      op->Call("DOM.describeNode", p, [=](bool ok, CefRefPtr<CefDictionaryValue> d) {
        CefRefPtr<CefDictionaryValue> desc =
            ok && d->GetType("node") == VTYPE_DICTIONARY ? d->GetDictionary("node") : nullptr;
        if (!desc) {
          op->Finish(ErrorJson("target_not_found", "the element under that point vanished"));
          return;
        }
        const std::string tag = desc->GetString("nodeName").ToString();
        const std::string content_frame = desc->GetString("frameId").ToString();
        // An <iframe> whose content frame is not in this DevTools target is a
        // cross-process boundary: hit testing stops at its element.
        const bool boundary = (tag == "IFRAME" || tag == "FRAME" || tag == "OBJECT" ||
                               tag == "EMBED" || tag == "FENCEDFRAME") &&
                              !content_frame.empty() && content_frame != hit_frame &&
                              !dev->count(content_frame);
        // DevTools chain main .. hit frame.
        std::vector<std::string> dchain;
        for (std::string id = hit_frame; !id.empty() && dchain.size() < 64; id = (*dev)[id].parent) {
          dchain.insert(dchain.begin(), id);
        }
        const std::vector<std::string> cchain = CefChain(*frames, frame_id);
        const size_t named_depth = cchain.size() - 1;
        const size_t visible_depth = dchain.size() - 1 + (boundary ? 1 : 0);
        if (named_depth < visible_depth || (named_depth > visible_depth && !boundary)) {
          op->Finish(ErrorJson("point_in_other_frame",
                               "the point is inside a child frame of the named frame"));
          return;
        }
        // Same-target frames must match the named frame's ancestors by URL.
        for (size_t i = 0; i < dchain.size(); ++i) {
          if (frames->at(cchain[i]).url != (*dev)[dchain[i]].url) {
            op->Finish(ErrorJson("point_in_other_frame",
                                 "the point is not inside the named frame"));
            return;
          }
        }
        // The CEF frame at each verified depth must be the only candidate.
        auto candidates = [&](size_t depth, const std::string& parent, auto pred) {
          int count = 0;
          for (const auto& [id, info] : *frames) {
            if (info.depth == static_cast<int>(depth) && info.parent == parent && pred(info)) ++count;
          }
          return count;
        };
        for (size_t i = 1; i < dchain.size(); ++i) {
          const auto& me = frames->at(cchain[i]);
          if (candidates(i, me.parent, [&](const CefFrameInfo& f) { return f.url == me.url; }) != 1) {
            op->Finish(ErrorJson("ambiguous_frame", "several identical frames share that position"));
            return;
          }
        }
        if (boundary) {
          const size_t i = dchain.size();
          const auto& me = frames->at(cchain[i]);
          if (!me.oopif) {
            op->Finish(ErrorJson("point_in_other_frame", "the point is not inside the named frame"));
            return;
          }
          // Several cross-process siblings: the element's src must name exactly ours.
          std::string src;
          if (desc->GetType("attributes") == VTYPE_LIST) {
            CefRefPtr<CefListValue> attrs = desc->GetList("attributes");
            for (size_t a = 0; a + 1 < attrs->GetSize(); a += 2) {
              if (attrs->GetString(a).ToString() == "src") src = attrs->GetString(a + 1).ToString();
            }
          }
          const int oopifs = candidates(i, me.parent, [](const CefFrameInfo& f) { return f.oopif; });
          if (oopifs != 1 &&
              (src != me.url || candidates(i, me.parent, [&](const CefFrameInfo& f) {
                 return f.oopif && f.url == src; }) != 1)) {
            op->Finish(ErrorJson("ambiguous_frame",
                                 "cannot tell which cross-process frame is under the point"));
            return;
          }
        }
        auto h = std::make_shared<InputHandle>();
        h->tab_id = op->tab_id;
        h->browser_id = op->browser->GetIdentifier();
        h->frame_id = frame_id;
        h->frame_url = frames->at(frame_id).url;
        h->cross_process = boundary;
        if (boundary) h->boundary_frame = content_frame;
        h->below_boundary = boundary && named_depth > visible_depth;
        h->node = node;
        h->mint_x = x;
        h->mint_y = y;
        for (const std::string& id : dchain) h->chain.push_back({id, (*dev)[id].loader, 0});
        if (boundary) h->chain.push_back({"", "", node});
        // Owners of the same-target frames, then the boxes.
        ResolveOwners(op, dchain, 1, std::make_shared<std::vector<int>>(), h,
                      [=](std::shared_ptr<std::vector<int>> owners) {
          if (boundary) owners->push_back(node);
          Viewport(op, [=](std::optional<Rect> viewport) {
            if (!viewport) {
              op->Finish(ErrorJson("document_unavailable", "tab viewport is unavailable"));
              return;
            }
            FrameRegion(op, *owners, *viewport, [=](std::optional<Rect> region) {
              if (!region) {
                std::string owners_json = "\"owners\":[";
                for (size_t i = 0; i < owners->size(); ++i) owners_json += (i ? "," : "") + std::to_string((*owners)[i]);
                op->Finish(ErrorJson("target_not_visible", "the frame is not visible",
                                     owners_json + "],\"viewport\":" + RectJson(*viewport)));
                return;
              }
              BorderBox(op, node, "border", [=](std::optional<Rect> box) {
                if (!box) {
                  op->Finish(ErrorJson("target_not_visible", "the element has no box"));
                  return;
                }
                h->frame_region = *region;
                h->node_box = *box;
                h->node_region = boundary ? *region : box->intersect(*region);
                if (!h->node_region.contains(x, y)) {
                  op->Finish(ErrorJson("target_not_visible",
                                       "the point is not on a visible part of the element",
                                       "\"element\":" + RectJson(*box) + ",\"frame\":" + RectJson(*region)));
                  return;
                }
                h->expires = Clock::now() + std::chrono::milliseconds(kHandleTtlMs);
                PruneHandles();
                const std::string id = NewHandleId();
                Handles()[id] = *h;
                std::ostringstream out;
                out << "{\"ok\":true,\"tabid\":" << op->tab_id << ",\"handle\":\"" << id
                    << "\",\"ttl_ms\":" << kHandleTtlMs << ",\"target\":{\"tag\":\""
                    << JsonEscape(boundary ? "IFRAME" : tag) << "\",\"bound\":\""
                    << (boundary ? "cross-process-frame" : "element") << "\"},\"frame\":{\"id\":\""
                    << JsonEscape(frame_id) << "\",\"depth\":" << named_depth
                    << ",\"verified_depth\":" << visible_depth
                    << ",\"inside_cross_process_frame\":" << (h->below_boundary ? "true" : "false")
                    << "},\"regions\":{\"element\":" << RectJson(h->node_region)
                    << ",\"frame\":" << RectJson(h->frame_region) << "}}";
                op->Finish(out.str());
              });
            });
          });
        });
      });
    });
  });
}

// --- pointer-gesture ----------------------------------------------------------

struct Gesture {
  std::shared_ptr<InputHandle> handle;
  std::string session;            // the bound OOPIF's own DevTools session, if attached
  std::vector<Step> steps;
  bool frame_bound = false;
  size_t down = SIZE_MAX;
  size_t next = 0;
  int dispatched = 0;
  bool held = false;
  std::optional<std::pair<double, double>> last;
};

void DispatchMouse(const OpPtr& op, std::shared_ptr<Gesture> g, StepKind kind, bool held,
                   double x, double y, InputDevTools::Result done) {
  if (!g->session.empty()) {
    // Frame-local coordinates for the iframe's own target.
    x -= g->handle->boundary_x;
    y -= g->handle->boundary_y;
  }
  auto p = Params();
  p->SetString("type", kind == StepKind::kDown ? "mousePressed"
                       : kind == StepKind::kUp ? "mouseReleased" : "mouseMoved");
  p->SetDouble("x", x);
  p->SetDouble("y", y);
  const bool pressed = kind == StepKind::kDown || (kind == StepKind::kMove && held);
  p->SetString("button", kind == StepKind::kMove && !held ? "none" : "left");
  p->SetInt("buttons", pressed ? 1 : 0);
  p->SetInt("clickCount", kind == StepKind::kMove ? 0 : 1);
  p->SetString("pointerType", "mouse");
  if (op->done) return;
  if (!g->session.empty()) {
    op->dt->CallSession(g->session, "Input.dispatchMouseEvent", p, std::move(done));
    return;
  }
  op->Call("Input.dispatchMouseEvent", p, std::move(done));
}

void FinishGesture(const OpPtr& op, std::shared_ptr<Gesture> g, std::string code,
                   std::string message) {
  auto report = [op, g, code, message](bool released) {
    std::ostringstream extra;
    extra << "\"gesture\":{\"dispatched_steps\":" << g->dispatched
          << ",\"button_released\":" << (released ? "true" : "false") << "}";
    op->Finish(ErrorJson(code, message, extra.str()));
  };
  if (g->held && g->last) {
    // Never leave a button pressed: release where the last validated event
    // went. This is not a replay of the refused step.
    g->held = false;
    DispatchMouse(op, g, StepKind::kUp, false, g->last->first, g->last->second,
                  [report](bool ok, CefRefPtr<CefDictionaryValue>) { report(ok); });
    return;
  }
  report(false);
}

void RunStep(const OpPtr& op, std::shared_ptr<Gesture> g);

void CheckAndDispatch(const OpPtr& op, std::shared_ptr<Gesture> g) {
  if (op->done) return;
  const Step step = g->steps[g->next];
  const InputHandle& h = *g->handle;
  const bool node_bound = !g->frame_bound || g->next <= g->down;
  const Rect& region = node_bound ? h.node_region : h.frame_region;
  if (!region.contains(step.x, step.y)) {
    FinishGesture(op, g, "outside_region", node_bound
        ? "a gesture point is outside the bound element"
        : "a gesture point is outside the bound frame");
    return;
  }
  HitTest(op, step.x, step.y, [op, g, step, node_bound](int node, std::string frame) {
    const InputHandle& h = *g->handle;
    auto dispatch = [op, g, step]() {
      DispatchMouse(op, g, step.kind, g->held, step.x, step.y,
                    [op, g, step](bool ok, CefRefPtr<CefDictionaryValue>) {
        if (!ok) {
          FinishGesture(op, g, "dispatch_failed", "the browser did not accept the input event; not retried");
          return;
        }
        ++g->dispatched;
        if (step.kind == StepKind::kDown) g->held = true;
        if (step.kind == StepKind::kUp) g->held = false;
        g->last = {step.x, step.y};
        if (++g->next >= g->steps.size()) {
          std::ostringstream out;
          out << "{\"ok\":true,\"tabid\":" << op->tab_id
              << ",\"target\":{\"kind\":\"region-handle\"},\"gesture\":{\"bound\":\""
              << (g->frame_bound ? "frame" : "element") << "\",\"route\":\""
              << (g->session.empty() ? "page" : "frame-target") << "\",\"dispatched_steps\":"
              << g->dispatched << ",\"trusted\":true}}";
          op->Finish(out.str());
          return;
        }
        CefPostDelayedTask(TID_UI, base::BindOnce([](OpPtr op, std::shared_ptr<Gesture> g) {
          RunStep(op, g);
        }, op, g), g->steps[g->next].delay_ms);
      });
    };
    if (!node) {
      FinishGesture(op, g, "target_obscured", "nothing hit-testable at a gesture point");
      return;
    }
    if (node_bound) {
      if (node != h.node) {
        FinishGesture(op, g, "target_obscured",
                      "a gesture point does not hit the bound element (covered or off it)");
        return;
      }
      dispatch();
      return;
    }
    // Frame-bound: the hit must be in the bound frame itself, not a child frame.
    const BoundFrame& bound = h.chain.back();
    if (h.cross_process) {
      if (node != bound.owner_node) {
        FinishGesture(op, g, "target_obscured", "a gesture point left the bound frame or is covered");
        return;
      }
      dispatch();
      return;
    }
    if (frame != bound.devtools_id) {
      FinishGesture(op, g, "target_obscured", "a gesture point left the bound frame or is covered");
      return;
    }
    auto p = Params();
    p->SetInt("backendNodeId", node);
    op->Call("DOM.describeNode", p, [op, g, frame, dispatch](bool ok, CefRefPtr<CefDictionaryValue> d) {
      CefRefPtr<CefDictionaryValue> desc =
          ok && d->GetType("node") == VTYPE_DICTIONARY ? d->GetDictionary("node") : nullptr;
      const std::string content = desc ? desc->GetString("frameId").ToString() : "";
      const std::string tag = desc ? desc->GetString("nodeName").ToString() : "";
      const bool owner = (tag == "IFRAME" || tag == "FRAME" || tag == "OBJECT" || tag == "EMBED" ||
                          tag == "FENCEDFRAME") && !content.empty() && content != frame;
      if (!desc || owner) {
        FinishGesture(op, g, "target_obscured", "a gesture point is inside a child frame");
        return;
      }
      dispatch();
    });
  });
}

void RunStep(const OpPtr& op, std::shared_ptr<Gesture> g) {
  if (op->done) return;
  CheckAndDispatch(op, g);
}

bool ParseSteps(CefRefPtr<CefDictionaryValue> root, Gesture* g, std::string* error) {
  auto fail = [error](std::string_view m) { *error = ErrorJson("invalid_gesture", m); return false; };
  if (root->HasKey("bound")) {
    const std::string bound = root->GetType("bound") == VTYPE_STRING ? root->GetString("bound").ToString() : "";
    if (bound != "element" && bound != "frame") return fail("bound must be \"element\" or \"frame\"");
    g->frame_bound = bound == "frame";
  }
  CefRefPtr<CefListValue> steps = root->GetType("steps") == VTYPE_LIST ? root->GetList("steps") : nullptr;
  if (!steps || steps->GetSize() == 0 || steps->GetSize() > kMaxSteps) {
    return fail("steps must be a list of 1 to 256 pointer steps");
  }
  int total = 0;
  bool up = false;
  for (size_t i = 0; i < steps->GetSize(); ++i) {
    CefRefPtr<CefDictionaryValue> s = steps->GetType(i) == VTYPE_DICTIONARY ? steps->GetDictionary(i) : nullptr;
    double x = 0, y = 0;
    if (!s || s->GetType("type") != VTYPE_STRING || !Number(s, "x", &x) || !Number(s, "y", &y) ||
        std::fabs(x) > 100000 || std::fabs(y) > 100000) {
      return fail("each step needs a type and finite viewport x/y in CSS px");
    }
    const std::string type = s->GetString("type").ToString();
    Step step{StepKind::kMove, x, y, 0};
    if (type == "down") step.kind = StepKind::kDown;
    else if (type == "up") step.kind = StepKind::kUp;
    else if (type != "move") return fail("step type must be move, down, or up");
    if (s->HasKey("delay_ms")) {
      if (s->GetType("delay_ms") != VTYPE_INT || s->GetInt("delay_ms") < 0 ||
          s->GetInt("delay_ms") > kMaxStepDelayMs) {
        return fail("delay_ms must be an integer from 0 to 5000");
      }
      step.delay_ms = s->GetInt("delay_ms");
    }
    if (up) return fail("gesture must be move* [down move* up]");
    if (step.kind == StepKind::kDown) {
      if (g->down != SIZE_MAX) return fail("gesture must be move* [down move* up]");
      g->down = i;
    }
    if (step.kind == StepKind::kUp) {
      if (g->down == SIZE_MAX) return fail("gesture must be move* [down move* up]");
      up = true;
    }
    total += step.delay_ms;
    g->steps.push_back(step);
  }
  if ((g->down != SIZE_MAX) != up) return fail("gesture must be move* [down move* up]");
  if (g->frame_bound && g->down == SIZE_MAX) return fail("a frame-bound gesture needs a press");
  if (total > kMaxGestureMs) return fail("a gesture may last at most 15 s");
  return true;
}

// --- key-input --------------------------------------------------------------

struct KeyStroke {
  std::string key, code, text;
  int vk = 0;
  std::string command;  // editing command, so editing keys work on every platform
};

std::optional<KeyStroke> NamedKey(const std::string& name) {
  static const std::map<std::string, KeyStroke> keys = {
      {"Enter", {"Enter", "Enter", "\r", 13, ""}}, {"Tab", {"Tab", "Tab", "", 9, ""}},
      {"Backspace", {"Backspace", "Backspace", "", 8, "deleteBackward"}},
      {"Delete", {"Delete", "Delete", "", 46, "deleteForward"}},
      {"Escape", {"Escape", "Escape", "", 27, ""}}, {"Space", {" ", "Space", " ", 32, ""}},
      {"ArrowLeft", {"ArrowLeft", "ArrowLeft", "", 37, "moveLeft"}},
      {"ArrowUp", {"ArrowUp", "ArrowUp", "", 38, "moveUp"}},
      {"ArrowRight", {"ArrowRight", "ArrowRight", "", 39, "moveRight"}},
      {"ArrowDown", {"ArrowDown", "ArrowDown", "", 40, "moveDown"}},
      {"Home", {"Home", "Home", "", 36, "moveToBeginningOfLine"}},
      {"End", {"End", "End", "", 35, "moveToEndOfLine"}}};
  auto it = keys.find(name);
  return it == keys.end() ? std::nullopt : std::optional<KeyStroke>(it->second);
}

// One keystroke per code point of printable text; US-layout codes for
// letters, digits and space, key + text only otherwise.
std::optional<std::vector<KeyStroke>> TextKeys(const std::string& text) {
  std::vector<KeyStroke> out;
  size_t i = 0;
  while (i < text.size()) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    const size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (!len || i + len > text.size()) return std::nullopt;
    for (size_t k = 1; k < len; ++k) {
      if ((static_cast<unsigned char>(text[i + k]) >> 6) != 2) return std::nullopt;
    }
    const std::string ch = text.substr(i, len);
    if (len == 1 && (c < 0x20 || c == 0x7f)) return std::nullopt;
    if (len == 2 && c == 0xC2 && static_cast<unsigned char>(text[i + 1]) < 0xA0) return std::nullopt;
    KeyStroke k{ch, "", ch, 0, ""};
    if (len == 1 && std::isalpha(c)) {
      k.code = std::string("Key") + static_cast<char>(std::toupper(c));
      k.vk = std::toupper(c);
    } else if (len == 1 && std::isdigit(c)) {
      k.code = std::string("Digit") + static_cast<char>(c);
      k.vk = c;
    } else if (c == ' ') {
      k.code = "Space";
      k.vk = 32;
    }
    out.push_back(k);
    i += len;
  }
  if (out.empty() || out.size() > static_cast<size_t>(kMaxKeyUnits)) return std::nullopt;
  return out;
}

struct Typing {
  std::shared_ptr<InputHandle> handle;
  std::vector<KeyStroke> keys;
  size_t next = 0;
};

void TypeNext(const OpPtr& op, std::shared_ptr<Typing> t);

std::string KeysError(std::shared_ptr<Typing> t, std::string_view code, std::string_view message) {
  return ErrorJson(code, message, "\"keys\":{\"dispatched_keys\":" + std::to_string(t->next) + "}");
}

bool FocusedIsBound(const OpPtr& op, const InputHandle& h) {
  CefRefPtr<CefFrame> focused = op->browser->GetFocusedFrame();
  return focused && focused->IsValid() && focused->GetIdentifier().ToString() == h.frame_id;
}

void TypeNext(const OpPtr& op, std::shared_ptr<Typing> t) {
  if (op->done) return;
  if (!FocusedIsBound(op, *t->handle)) {
    op->Finish(KeysError(t, "frame_not_focused",
                         "the handle's frame is not the tab's focused frame; click the field first"));
    return;
  }
  const KeyStroke k = t->keys[t->next];
  auto params = [k](const char* type) {
    auto p = Params();
    p->SetString("type", type);
    p->SetString("key", k.key);
    if (!k.code.empty()) p->SetString("code", k.code);
    // No nativeVirtualKeyCode: without it DevTools marks the event
    // skip_if_unhandled, so it reaches only the page, never browser shortcuts
    // or vimbrowser's own key bindings.
    if (k.vk) p->SetInt("windowsVirtualKeyCode", k.vk);
    if (std::string(type) == "keyDown" && !k.text.empty()) {
      p->SetString("text", k.text);
      p->SetString("unmodifiedText", k.text);
    }
    if (std::string(type) == "rawKeyDown" && !k.command.empty()) {
      CefRefPtr<CefListValue> commands = CefListValue::Create();
      commands->SetString(0, k.command);
      p->SetList("commands", commands);
    }
    if (k.key.size() == 1 && std::isupper(static_cast<unsigned char>(k.key[0]))) {
      p->SetInt("modifiers", 8);  // Shift
    }
    return p;
  };
  op->Call("Input.dispatchKeyEvent", params(k.text.empty() ? "rawKeyDown" : "keyDown"),
           [op, t, params](bool ok, CefRefPtr<CefDictionaryValue>) {
    if (!ok) {
      op->Finish(KeysError(t, "dispatch_failed", "the browser did not accept the key event; not retried"));
      return;
    }
    op->Call("Input.dispatchKeyEvent", params("keyUp"), [op, t](bool ok, CefRefPtr<CefDictionaryValue>) {
      ++t->next;
      if (!ok) {
        op->Finish(KeysError(t, "dispatch_failed", "the browser did not accept the key event; not retried"));
        return;
      }
      if (t->next >= t->keys.size()) {
        op->Finish("{\"ok\":true,\"tabid\":" + std::to_string(op->tab_id) +
                   ",\"target\":{\"kind\":\"region-handle\"},\"keys\":{\"dispatched_keys\":" +
                   std::to_string(t->next) + ",\"trusted\":true}}");
        return;
      }
      CefPostDelayedTask(TID_UI, base::BindOnce([](OpPtr op, std::shared_ptr<Typing> t) {
        TypeNext(op, t);
      }, op, t), kKeyIntervalMs);
    });
  });
}

}  // namespace

void BrowserWindow::HandleInputIpcCommand(const std::string& command,
                                          uint64_t tab_id,
                                          const std::string& encoded,
                                          IpcReplyCallback reply) {
  std::string error;
  CefRefPtr<CefDictionaryValue> root = DecodePayload(encoded, &error);
  if (!root) {
    reply(error);
    return;
  }
  std::string browser_error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &browser_error);
  if (!browser || !browser->GetHost()) {
    reply(ErrorJson("tab_unavailable", "tab has no live browser backend"));
    return;
  }
  auto op = std::make_shared<Op>();
  op->tab_id = tab_id;
  op->browser = browser;
  op->reply = std::move(reply);

  if (command == "input-region") {
    double x = 0, y = 0;
    const std::string frame_id =
        root->GetType("frame_id") == VTYPE_STRING ? root->GetString("frame_id").ToString() : "";
    if (frame_id.empty() || frame_id.size() > 256 || !Number(root, "x", &x) ||
        !Number(root, "y", &y) || x < 0 || y < 0 || x > 100000 || y > 100000) {
      op->Finish(ErrorJson("invalid_payload", "input-region needs frame_id and a viewport x/y"));
      return;
    }
    op->dt = new InputDevTools(browser->GetHost());
    MintRegion(op, frame_id, x, y);
    return;
  }

  std::string handle_id;
  if (!HandleArg(root, &handle_id, &error)) {
    op->Finish(error);
    return;
  }
  // Shape errors are reported before the handle is spent.
  auto gesture = std::make_shared<Gesture>();
  auto typing = std::make_shared<Typing>();
  if (command == "pointer-gesture") {
    if (!ParseSteps(root, gesture.get(), &error)) {
      op->Finish(error);
      return;
    }
  } else {
    const bool has_text = root->HasKey("text"), has_key = root->HasKey("key");
    std::optional<std::vector<KeyStroke>> keys;
    if (has_text && !has_key && root->GetType("text") == VTYPE_STRING) {
      keys = TextKeys(root->GetString("text").ToString());
    } else if (has_key && !has_text && root->GetType("key") == VTYPE_STRING) {
      if (auto k = NamedKey(root->GetString("key").ToString())) keys = std::vector<KeyStroke>{*k};
    }
    if (!keys) {
      op->Finish(ErrorJson("invalid_gesture", "key input must be printable text or one named key"));
      return;
    }
    typing->keys = std::move(*keys);
  }
  std::string code;
  std::optional<InputHandle> handle = ConsumeHandle(handle_id, &code);
  if (!handle) {
    op->Finish(ErrorJson(code, code == "expired_handle" ? "input-region handle expired"
                                                         : "input-region handle is unknown or already used"));
    return;
  }
  if (handle->tab_id != tab_id || handle->browser_id != browser->GetIdentifier()) {
    op->Finish(ErrorJson("invalid_handle", "input-region handle belongs to another tab"));
    return;
  }
  auto h = std::make_shared<InputHandle>(std::move(*handle));
  op->dt = new InputDevTools(browser->GetHost());
  if (command == "pointer-gesture") {
    gesture->handle = h;
    Revalidate(op, h, [op, gesture](std::string code) {
      if (!code.empty()) {
        op->Finish(ErrorJson(code, code == "target_moved"
            ? "the bound element moved since input-region; mint a new handle"
            : "the bound frame or its document changed", "\"gesture\":{\"dispatched_steps\":0,\"button_released\":false}"));
        return;
      }
      auto start = [op, gesture]() {
        CefPostDelayedTask(TID_UI, base::BindOnce([](OpPtr op, std::shared_ptr<Gesture> g) {
          RunStep(op, g);
        }, op, gesture), gesture->steps[0].delay_ms);
      };
      std::shared_ptr<InputHandle> h = gesture->handle;
      if (!h->cross_process || h->boundary_frame.empty()) {
        start();
        return;
      }
      BorderBox(op, h->node, "content", [op, gesture, h, start](std::optional<Rect> content) {
        if (!content) {
          start();  // page-target route
          return;
        }
        h->boundary_x = content->x0;
        h->boundary_y = content->y0;
        auto p = Params();
        p->SetBool("autoAttach", true);
        p->SetBool("waitForDebuggerOnStart", false);
        p->SetBool("flatten", true);
        op->dt->ExpectAttach();
        op->Call("Target.setAutoAttach", p, [op, gesture, h, start](bool, CefRefPtr<CefDictionaryValue>) {
          // Without a session the gesture still works through the page target.
          gesture->session = op->dt ? op->dt->SessionFor(h->boundary_frame) : "";
          start();
        });
      });
    });
    return;
  }
  typing->handle = h;
  Revalidate(op, h, [op, typing, h](std::string code) {
    if (!code.empty()) {
      op->Finish(KeysError(typing, code, "the bound field, frame or document changed"));
      return;
    }
    // The field must still be what is under the minted point.
    HitTest(op, h->mint_x, h->mint_y, [op, typing, h](int node, std::string) {
      if (node != h->node) {
        op->Finish(KeysError(typing, "target_obscured", "the bound field is covered or gone"));
        return;
      }
      TypeNext(op, typing);
    });
  });
}

}  // namespace vimbrowser
