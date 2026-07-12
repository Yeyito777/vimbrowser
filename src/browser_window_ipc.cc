#include "browser_window.h"
#include "browser_window_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "browser_client.h"
#include "config.h"
#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "include/cef_cookie.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_navigation_entry.h"
#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/cef_string_visitor.h"
#include "include/cef_urlrequest.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_closure_task.h"

namespace vimbrowser {
namespace {

int NextScreenshotDevToolsMessageId() {
  // Use an explicit positive ID instead of ExecuteDevToolsMethod(0)'s auto-ID so
  // an extremely fast DevTools method result cannot race observer initialization.
  static int next_message_id = 900000000;
  if (next_message_id >= 999000000) {
    next_message_id = 900000000;
  }
  return next_message_id++;
}

class IpcStringVisitor final : public CefStringVisitor {
 public:
  explicit IpcStringVisitor(IpcReplyCallback reply) : reply_(std::move(reply)) {}

  void Visit(const CefString& string) override {
    if (reply_) {
      reply_(string.ToString());
      reply_ = nullptr;
    }
  }

 private:
  IpcReplyCallback reply_;

  IMPLEMENT_REFCOUNTING(IpcStringVisitor);
  DISALLOW_COPY_AND_ASSIGN(IpcStringVisitor);
};

class CookieListVisitor final : public CefCookieVisitor {
 public:
  explicit CookieListVisitor(IpcReplyCallback reply) : reply_(std::move(reply)) {}

  bool Visit(const CefCookie& cookie,
             int count,
             int total,
             bool& deleteCookie) override {
    deleteCookie = false;
    cookies_.push_back(CookieJson(cookie));
    if (total <= 0 || count + 1 >= total) {
      Finish();
      return false;
    }
    return true;
  }

  void Finish() {
    if (!reply_) {
      return;
    }
    std::ostringstream out;
    out << "{\"cookies\":[";
    for (size_t i = 0; i < cookies_.size(); ++i) {
      if (i) {
        out << ",";
      }
      out << cookies_[i];
    }
    out << "]}";
    auto reply = std::move(reply_);
    reply_ = nullptr;
    reply(out.str());
  }

 private:
  IpcReplyCallback reply_;
  std::vector<std::string> cookies_;

  IMPLEMENT_REFCOUNTING(CookieListVisitor);
  DISALLOW_COPY_AND_ASSIGN(CookieListVisitor);
};

class CookieDeleteCallback final : public CefDeleteCookiesCallback {
 public:
  explicit CookieDeleteCallback(IpcReplyCallback reply) : reply_(std::move(reply)) {}

  void OnComplete(int num_deleted) override {
    if (reply_) {
      reply_("{\"deleted\":" + std::to_string(num_deleted) + "}");
      reply_ = nullptr;
    }
  }

 private:
  IpcReplyCallback reply_;

  IMPLEMENT_REFCOUNTING(CookieDeleteCallback);
  DISALLOW_COPY_AND_ASSIGN(CookieDeleteCallback);
};

class CookieSetCallback final : public CefSetCookieCallback {
 public:
  explicit CookieSetCallback(IpcReplyCallback reply) : reply_(std::move(reply)) {}

  void OnComplete(bool success) override {
    if (reply_) {
      reply_(std::string("{\"success\":") + (success ? "true" : "false") + "}");
      reply_ = nullptr;
    }
  }

 private:
  IpcReplyCallback reply_;

  IMPLEMENT_REFCOUNTING(CookieSetCallback);
  DISALLOW_COPY_AND_ASSIGN(CookieSetCallback);
};

void VisitCookiesForUrl(CefRefPtr<CefCookieManager> manager,
                        const std::string& url,
                        IpcReplyCallback reply) {
  if (url.empty()) {
    reply("ERR cookie URL is empty\n");
    return;
  }
  if (!manager) {
    reply("ERR no cookie manager\n");
    return;
  }
  CefRefPtr<CookieListVisitor> visitor(new CookieListVisitor(std::move(reply)));
  if (!manager->VisitUrlCookies(url, true, visitor)) {
    visitor->Finish();
    return;
  }
  CefPostDelayedTask(TID_UI, base::BindOnce(&CookieListVisitor::Finish, visitor), 1500);
}

class URLRequestReplayClient final : public CefURLRequestClient {
 public:
  explicit URLRequestReplayClient(IpcReplyCallback reply)
      : reply_(std::move(reply)) {}

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    if (!reply_) {
      return;
    }
    CefRefPtr<CefResponse> response = request ? request->GetResponse() : nullptr;
    CefResponse::HeaderMap headers;
    if (response) {
      response->GetHeaderMap(headers);
    }
    std::ostringstream out;
    out << "{"
        << "\"request_status\":"
        << (request ? static_cast<int>(request->GetRequestStatus()) : -1) << ","
        << "\"error\":" << (response ? static_cast<int>(response->GetError()) : 0) << ","
        << "\"status\":" << (response ? response->GetStatus() : 0) << ","
        << "\"status_text\":\""
        << JsonEscape(response ? response->GetStatusText().ToString() : std::string())
        << "\","
        << "\"mime_type\":\""
        << JsonEscape(response ? response->GetMimeType().ToString() : std::string())
        << "\","
        << "\"url\":\""
        << JsonEscape(response ? response->GetURL().ToString() : std::string())
        << "\","
        << "\"headers\":" << HeadersJson(headers) << ","
        << "\"body\":\"" << JsonEscape(body_) << "\","
        << "\"body_size\":" << body_.size() << ","
        << "\"body_truncated\":" << (body_truncated_ ? "true" : "false")
        << "}";
    auto reply = std::move(reply_);
    reply_ = nullptr;
    reply(out.str());
  }

  void OnUploadProgress(CefRefPtr<CefURLRequest> request,
                        int64_t current,
                        int64_t total) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest> request,
                          int64_t current,
                          int64_t total) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest> request,
                      const void* data,
                      size_t data_length) override {
    const size_t remaining = body_.size() < (1024 * 1024)
                                 ? (1024 * 1024) - body_.size()
                                 : 0;
    const size_t take = std::min(data_length, remaining);
    if (take > 0) {
      body_.append(static_cast<const char*>(data), take);
    }
    if (take < data_length) {
      body_truncated_ = true;
    }
  }
  bool GetAuthCredentials(bool isProxy,
                          const CefString& host,
                          int port,
                          const CefString& realm,
                          const CefString& scheme,
                          CefRefPtr<CefAuthCallback> callback) override {
    return false;
  }

 private:
  IpcReplyCallback reply_;
  std::string body_;
  bool body_truncated_ = false;

  IMPLEMENT_REFCOUNTING(URLRequestReplayClient);
  DISALLOW_COPY_AND_ASSIGN(URLRequestReplayClient);
};

class ScreenshotDevToolsObserver final : public CefDevToolsMessageObserver {
 public:
  ScreenshotDevToolsObserver(uint64_t tab_id,
                             std::string url,
                             IpcReplyCallback reply,
                             std::function<void()> cleanup = {})
      : tab_id_(tab_id),
        url_(std::move(url)),
        reply_(std::move(reply)),
        cleanup_(std::move(cleanup)) {}

  void SetRegistration(CefRefPtr<CefRegistration> registration) {
    registration_ = registration;
  }

  void SetMessageId(int message_id) { message_id_ = message_id; }

  void Fail(std::string error) { Finish(std::move(error)); }

  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser,
                              int message_id,
                              bool success,
                              const void* result,
                              size_t result_size) override {
    if (completed_ || message_id_ == 0 || message_id != message_id_) {
      return;
    }

    if (!success) {
      Finish("ERR screenshot failed: " + DevToolsErrorMessage(result, result_size) +
             "\n");
      return;
    }

    CefRefPtr<CefValue> value = CefParseJSON(result, result_size, JSON_PARSER_RFC);
    if (!value || value->GetType() != VTYPE_DICTIONARY) {
      Finish("ERR screenshot failed: invalid devtools response\n");
      return;
    }
    CefRefPtr<CefDictionaryValue> dict = value->GetDictionary();
    if (!dict || !dict->HasKey("data") || dict->GetType("data") != VTYPE_STRING) {
      Finish("ERR screenshot failed: devtools response did not include image data\n");
      return;
    }

    const std::string data = dict->GetString("data").ToString();
    if (data.empty()) {
      Finish("ERR screenshot failed: empty image data\n");
      return;
    }

    std::ostringstream out;
    out << "{"
        << "\"tabid\":" << tab_id_ << ","
        << "\"url\":\"" << JsonEscape(url_) << "\","
        << "\"mime_type\":\"image/png\","
        << "\"encoding\":\"base64\","
        << "\"data\":\"" << JsonEscape(data) << "\""
        << "}";
    Finish(out.str());
  }

 private:
  std::string DevToolsErrorMessage(const void* result, size_t result_size) const {
    if (!result || result_size == 0) {
      return "unknown error";
    }
    CefRefPtr<CefValue> value = CefParseJSON(result, result_size, JSON_PARSER_RFC);
    if (value && value->GetType() == VTYPE_DICTIONARY) {
      CefRefPtr<CefDictionaryValue> dict = value->GetDictionary();
      if (dict && dict->HasKey("message") &&
          dict->GetType("message") == VTYPE_STRING) {
        return dict->GetString("message").ToString();
      }
    }
    return std::string(static_cast<const char*>(result), result_size);
  }

  void Finish(std::string response) {
    if (completed_) {
      return;
    }
    completed_ = true;
    registration_ = nullptr;
    if (cleanup_) {
      auto cleanup = std::move(cleanup_);
      cleanup();
    }
    if (reply_) {
      auto reply = std::move(reply_);
      reply(std::move(response));
    }
  }

  uint64_t tab_id_ = 0;
  std::string url_;
  IpcReplyCallback reply_;
  std::function<void()> cleanup_;
  CefRefPtr<CefRegistration> registration_;
  int message_id_ = 0;
  bool completed_ = false;

  IMPLEMENT_REFCOUNTING(ScreenshotDevToolsObserver);
  DISALLOW_COPY_AND_ASSIGN(ScreenshotDevToolsObserver);
};

void StartScreenshotDevToolsCapture(CefRefPtr<CefBrowserHost> host,
                                    CefRefPtr<CefDictionaryValue> params,
                                    CefRefPtr<ScreenshotDevToolsObserver> observer) {
  if (!host || !observer) {
    if (observer) {
      observer->Fail("ERR screenshot failed to start\n");
    }
    return;
  }

  observer->SetRegistration(host->AddDevToolsMessageObserver(observer));
  const int requested_message_id = NextScreenshotDevToolsMessageId();
  observer->SetMessageId(requested_message_id);
  const int message_id =
      host->ExecuteDevToolsMethod(requested_message_id, "Page.captureScreenshot",
                                  params);
  if (message_id == 0) {
    observer->Fail("ERR screenshot failed to start\n");
    return;
  }
  if (message_id != requested_message_id) {
    observer->SetMessageId(message_id);
  }
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(
          [](CefRefPtr<ScreenshotDevToolsObserver> observer) {
            if (observer) {
              observer->Fail("ERR screenshot timed out\n");
            }
          },
          observer),
      30000);
}


}  // namespace

void BrowserWindow::CompleteJsIpcRequest(uint64_t request_id, std::string response) {
  auto it = pending_js_ipc_.find(request_id);
  if (it == pending_js_ipc_.end()) {
    return;
  }
  IpcReplyCallback reply = std::move(it->second);
  pending_js_ipc_.erase(it);
  reply(std::move(response));
}

void BrowserWindow::HandleHtmlIpcCommand(uint64_t tab_id,
                                         bool text,
                                         IpcReplyCallback reply) {
  std::string error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error);
  if (!browser) {
    reply(error);
    return;
  }
  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame) {
    reply("ERR tab has no main frame\n");
    return;
  }
  CefRefPtr<IpcStringVisitor> visitor(new IpcStringVisitor(std::move(reply)));
  if (text) {
    frame->GetText(visitor);
  } else {
    frame->GetSource(visitor);
  }
}

void BrowserWindow::HandleJsIpcCommand(uint64_t tab_id,
                                       std::string code,
                                       IpcReplyCallback reply,
                                       int timeout_ms) {
  std::string error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error);
  if (!browser) {
    reply(error);
    return;
  }
  CefRefPtr<CefFrame> frame = browser->GetMainFrame();
  if (!frame) {
    reply("ERR tab has no main frame\n");
    return;
  }

  const uint64_t request_id = next_ipc_request_id_++;
  pending_js_ipc_[request_id] = std::move(reply);

  CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create(kJsEvalMessage);
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  args->SetString(0, std::to_string(request_id));
  args->SetString(1, code);
  frame->SendProcessMessage(PID_RENDERER, message);

  CefRefPtr<BrowserWindow> self = this;
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(&BrowserWindow::CompleteJsIpcRequest, self, request_id,
                     std::string("ERR js command timed out\n")),
      timeout_ms);
}

void BrowserWindow::HandleCookiesIpcCommand(uint64_t tab_id,
                                            std::string url_override,
                                            IpcReplyCallback reply) {
  std::string error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error);
  if (!browser) {
    reply(error);
    return;
  }
  std::string url = std::move(url_override);
  if (url.empty()) {
    url = browser->GetMainFrame() ? browser->GetMainFrame()->GetURL().ToString()
                                  : std::string();
  }
  if (url.empty()) {
    reply("ERR tab has no url\n");
    return;
  }
  CefRefPtr<CefRequestContext> context = browser->GetHost()
                                             ? browser->GetHost()->GetRequestContext()
                                             : nullptr;
  CefRefPtr<CefCookieManager> manager = context ? context->GetCookieManager(nullptr)
                                                : nullptr;
  VisitCookiesForUrl(manager, url, std::move(reply));
}

void BrowserWindow::HandleCookiesForUrlIpcCommand(std::string url,
                                                  IpcReplyCallback reply) {
  CefRefPtr<CefCookieManager> manager = CefCookieManager::GetGlobalManager(nullptr);
  VisitCookiesForUrl(manager, url, std::move(reply));
}

void BrowserWindow::HandleCookieDeleteIpcCommand(uint64_t tab_id,
                                                std::string name,
                                                IpcReplyCallback reply) {
  std::string error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error);
  if (!browser) {
    reply(error);
    return;
  }
  const std::string url = browser->GetMainFrame()
                              ? browser->GetMainFrame()->GetURL().ToString()
                              : std::string();
  if (url.empty()) {
    reply("ERR tab has no url\n");
    return;
  }
  CefRefPtr<CefRequestContext> context = browser->GetHost()
                                             ? browser->GetHost()->GetRequestContext()
                                             : nullptr;
  CefRefPtr<CefCookieManager> manager = context ? context->GetCookieManager(nullptr)
                                                : nullptr;
  if (!manager) {
    reply("ERR no cookie manager\n");
    return;
  }
  if (!manager->DeleteCookies(url, name, new CookieDeleteCallback(std::move(reply)))) {
    reply("ERR cookie delete failed to start\n");
  }
}

void BrowserWindow::HandleCookieSetIpcCommand(uint64_t tab_id,
                                             std::string name,
                                             std::string value,
                                             std::string domain,
                                             std::string path,
                                             IpcReplyCallback reply) {
  std::string error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error);
  if (!browser) {
    reply(error);
    return;
  }
  const std::string url = browser->GetMainFrame()
                              ? browser->GetMainFrame()->GetURL().ToString()
                              : std::string();
  if (url.empty()) {
    reply("ERR tab has no url\n");
    return;
  }
  CefRefPtr<CefRequestContext> context = browser->GetHost()
                                             ? browser->GetHost()->GetRequestContext()
                                             : nullptr;
  CefRefPtr<CefCookieManager> manager = context ? context->GetCookieManager(nullptr)
                                                : nullptr;
  if (!manager) {
    reply("ERR no cookie manager\n");
    return;
  }
  CefCookie cookie;
  CefString(&cookie.name).FromString(name);
  CefString(&cookie.value).FromString(value);
  CefString(&cookie.domain).FromString(domain);
  CefString(&cookie.path).FromString(path.empty() ? "/" : path);
  cookie.secure = StartsWithCaseInsensitive(url, "https://") ? 1 : 0;
  cookie.httponly = 0;
  cookie.has_expires = 0;
  cookie.same_site = CEF_COOKIE_SAME_SITE_UNSPECIFIED;
  if (!manager->SetCookie(url, cookie, new CookieSetCallback(std::move(reply)))) {
    reply("ERR cookie set failed to start\n");
  }
}

void BrowserWindow::HandleNetworkReplayIpcCommand(uint64_t tab_id,
                                                  uint64_t request_id,
                                                  IpcReplyCallback reply) {
  std::string error;
  size_t index = 0;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error, &index);
  if (!browser) {
    reply(error);
    return;
  }
  if (!tabs_[index].client) {
    reply("ERR tab has no client\n");
    return;
  }
  CefRefPtr<CefRequest> request = tabs_[index].client->BuildReplayRequest(request_id, &error);
  if (!request) {
    reply(error);
    return;
  }
  CefRefPtr<URLRequestReplayClient> client(new URLRequestReplayClient(std::move(reply)));
  CefRefPtr<CefRequestContext> context = browser->GetHost()
                                             ? browser->GetHost()->GetRequestContext()
                                             : nullptr;
  CefURLRequest::Create(request, client, context);
}

void BrowserWindow::HandleScreenshotIpcCommand(uint64_t tab_id,
                                               IpcReplyCallback reply) {
  std::string error;
  size_t index = 0;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error, &index);
  if (!browser) {
    reply(error);
    return;
  }
  CefRefPtr<CefBrowserHost> host = browser->GetHost();
  if (!host) {
    reply("ERR tab has no browser host\n");
    return;
  }

  std::string url = tabs_[index].url;
  if (browser->GetMainFrame()) {
    const std::string frame_url = browser->GetMainFrame()->GetURL().ToString();
    if (!frame_url.empty()) {
      url = frame_url;
    }
  }

  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString("format", "png");
  params->SetBool("fromSurface", true);
  params->SetBool("captureBeyondViewport", false);
  params->SetBool("optimizeForSpeed", true);

  if (index == active_index_) {
    CefRefPtr<ScreenshotDevToolsObserver> observer =
        new ScreenshotDevToolsObserver(tab_id, std::move(url), std::move(reply));
    StartScreenshotDevToolsCapture(host, params, observer);
    return;
  }

  // Paint the inactive tab into a background compositor surface without
  // activating or focusing it. CEF/Views only keeps a reliable surface for views
  // that are attached and visible, so briefly show the target behind the active
  // view, keep the active view frontmost, then let the backend CDP new-surface
  // path copy the target's own surface. The cleanup hides the target again if it
  // is still inactive.
  CefRefPtr<CefBrowserView> target_view = tabs_[index].view;
  CefRefPtr<CefBrowserView> active_view =
      active_index_ < tabs_.size() ? tabs_[active_index_].view : nullptr;
  if (target_view) {
    target_view->SetVisible(true);
  }
  if (content_inner_panel_ && active_view && active_view != target_view) {
    content_inner_panel_->ReorderChildView(active_view, -1);
  }
  if (content_inner_panel_ && content_inner_panel_->GetLayout()) {
    content_inner_panel_->Layout();
  }

  CefRefPtr<BrowserWindow> self(this);
  auto cleanup = [self, target_view, tab_id]() {
    const std::optional<size_t> index = self->FindTabIndexById(tab_id);
    if (!index || *index != self->active_index_) {
      if (target_view) {
        target_view->SetVisible(false);
      }
    }
    if (self->content_inner_panel_ && self->content_inner_panel_->GetLayout()) {
      self->content_inner_panel_->Layout();
    }
  };

  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(
          [](CefRefPtr<CefBrowserHost> host,
             CefRefPtr<CefDictionaryValue> params,
             uint64_t tab_id,
             std::string url,
             IpcReplyCallback reply,
             std::function<void()> cleanup) mutable {
            CefRefPtr<ScreenshotDevToolsObserver> observer =
                new ScreenshotDevToolsObserver(tab_id, std::move(url),
                                               std::move(reply),
                                               std::move(cleanup));
            StartScreenshotDevToolsCapture(host, params, observer);
          },
          host, params, tab_id, std::move(url), std::move(reply),
          std::move(cleanup)),
      75);
}

void BrowserWindow::AppendTabJson(std::string& out,
                                  const Tab& tab,
                                  size_t index) const {
  if (!tab.client && !tab.url.empty()) {
    out += "{\"id\":";
    out += tab.id_json;
    out += ",\"index\":";
    AppendJsonNumber(out, index);
    out += ",\"tab\":";
    AppendJsonNumber(out, index + 1);
    out += ",\"active\":";
    AppendJsonBool(out, index == active_index_);
    out += ",\"audible\":";
    AppendJsonBool(out, tab.audible);
    out += ",\"folder_id\":";
    AppendJsonNumber(out, tab.folder_id);
    out += ",\"sidebar_sort_order\":";
    AppendJsonNumber(out, tab.sidebar_sort_order);
    out += ",\"pinned\":";
    AppendJsonBool(out, tab.pinned);
    out += ",\"context\":";
    if (tab.context.empty()) {
      out += "null";
    } else {
      AppendJsonString(out, tab.context);
    }
    out += ",\"url\":";
    out += tab.url_json;
    out += ",\"title\":\"\",\"loading\":false,\"can_go_back\":false,"
           "\"can_go_forward\":false,\"fps_has_sample\":false,\"fps\":null,"
           "\"refresh_rate\":0}";
    return;
  }

  bool fps_has_sample = false;
  double fps = 0.0;
  double refresh_rate = 0.0;
  bool loading = false;
  bool can_go_back = false;
  bool can_go_forward = false;
  std::string url;
  std::string title;

  CefRefPtr<CefBrowser> browser = tab.client ? tab.client->browser() : nullptr;
  if (tab.client) {
    fps_has_sample = tab.client->fps_has_sample();
    fps = tab.client->current_fps();
    refresh_rate = tab.client->compositor_refresh_rate();
  }
  if (browser) {
    loading = browser->IsLoading();
    can_go_back = browser->CanGoBack();
    can_go_forward = browser->CanGoForward();
    if (tab.url.empty() && browser->GetMainFrame()) {
      const std::string frame_url = browser->GetMainFrame()->GetURL().ToString();
      if (!frame_url.empty()) {
        url = frame_url;
      }
    }
    if (browser->GetHost()) {
      CefRefPtr<CefNavigationEntry> entry =
          browser->GetHost()->GetVisibleNavigationEntry();
      if (entry) {
        title = entry->GetTitle().ToString();
      }
    }
  }

  out += "{\"id\":";
  if (!tab.id_json.empty()) {
    out += tab.id_json;
  } else {
    AppendJsonNumber(out, tab.id);
  }
  out += ",\"index\":";
  AppendJsonNumber(out, index);
  out += ",\"tab\":";
  AppendJsonNumber(out, index + 1);
  out += ",\"active\":";
  AppendJsonBool(out, index == active_index_);
  out += ",\"audible\":";
  AppendJsonBool(out, tab.audible);
  out += ",\"folder_id\":";
  AppendJsonNumber(out, tab.folder_id);
  out += ",\"sidebar_sort_order\":";
  AppendJsonNumber(out, tab.sidebar_sort_order);
  out += ",\"pinned\":";
  AppendJsonBool(out, tab.pinned);
  out += ",\"context\":";
  if (tab.context.empty()) {
    out += "null";
  } else {
    AppendJsonString(out, tab.context);
  }
  out += ",\"url\":";
  if (!tab.url.empty()) {
    out += tab.url_json;
  } else {
    AppendJsonString(out, url);
  }
  out += ",\"title\":";
  AppendJsonString(out, title);
  out += ",\"loading\":";
  AppendJsonBool(out, loading);
  out += ",\"can_go_back\":";
  AppendJsonBool(out, can_go_back);
  out += ",\"can_go_forward\":";
  AppendJsonBool(out, can_go_forward);
  out += ",\"fps_has_sample\":";
  AppendJsonBool(out, fps_has_sample);
  out += ",\"fps\":";
  if (fps_has_sample) {
    AppendJsonNumber(out, static_cast<int>(std::round(fps)));
  } else {
    out += "null";
  }
  out += ",\"refresh_rate\":";
  if (refresh_rate == 0.0) {
    out.push_back('0');
  } else {
    AppendJsonNumber(out, refresh_rate);
  }
  out += "}";
}

std::string BrowserWindow::TabsJson() const {
  std::string out;
  out.reserve(96 + tabs_.size() * 240);
  out += "{\"ipc_protocol\":\"";
  out += kIpcProtocolName;
  out += "\",\"ipc_version\":";
  AppendJsonNumber(out, kIpcProtocolVersion);
  out += ",\"active_tabid\":";
  AppendJsonNumber(out, ActiveTabId());
  out += ",\"active_index\":";
  AppendJsonNumber(out, active_index_);
  out += ",\"active_tab\":";
  AppendJsonNumber(out, active_index_ + 1);
  out += ",\"current_folder_id\":";
  AppendJsonNumber(out, current_sidebar_folder_id_);
  out += ",\"sidebar_selected_type\":";
  switch (sidebar_selected_item_.type) {
    case SidebarItemType::kParent:
      AppendJsonString(out, "parent");
      break;
    case SidebarItemType::kFolder:
      AppendJsonString(out, "folder");
      break;
    case SidebarItemType::kTab:
      AppendJsonString(out, "tab");
      break;
    case SidebarItemType::kNone:
      AppendJsonString(out, "none");
      break;
  }
  out += ",\"sidebar_selected_id\":";
  AppendJsonNumber(out, sidebar_selected_item_.id);
  out += ",\"tabs\":[";
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (i > 0) {
      out.push_back(',');
    }
    AppendTabJson(out, tabs_[i], i);
  }
  out += "]}";
  return out;
}


std::string BrowserWindow::HandleIpcCommand(const std::string& command_line) {
  const std::vector<std::string> argv = SplitArgs(command_line);
  if (argv.empty()) {
    return "ERR empty command\n";
  }

  const std::string command = ToLowerAscii(argv[0]);
  if (command == "version" || command == "protocol") {
    return IpcVersionJson();
  }
  if (command == "status" || command == "json") {
    RefreshAudibleTabs();
    return IpcStatusJson();
  }
  if (command == "tabs") {
    RefreshAudibleTabs();
    return TabsJson();
  }
  if (command == "commands") {
    return IpcCommandsJson();
  }
  if (command == "folders") {
    return FoldersJson();
  }
  if (command == "sidebar") {
    EnsureSidebarSelection();
    return SidebarJson();
  }
  if (command == "fps") {
    if (Tab* tab = ActiveTab(); tab && tab->client && tab->client->fps_has_sample()) {
      return std::to_string(static_cast<int>(std::round(tab->client->current_fps())));
    }
    return "--";
  }
  if (command == "refresh") {
    if (Tab* tab = ActiveTab(); tab && tab->client) {
      return std::to_string(tab->client->compositor_refresh_rate()) + "\n";
    }
    return "0\n";
  }
  if (command == "url") {
    return ActiveTabUrl();
  }
  auto find_tab_index_arg = [&](const std::string& text,
                                std::string* error) -> std::optional<size_t> {
    uint64_t tab_id = 0;
    if (!ParseUint64Arg(text, &tab_id) || tab_id == 0) {
      if (error) *error = "ERR invalid tabid\n";
      return std::nullopt;
    }
    std::optional<size_t> index = FindTabIndexById(tab_id);
    if (!index && error) {
      *error = "ERR no such tabid\n";
    }
    return index;
  };
  auto tab_index_or_active = [&](size_t arg_index,
                                 std::string* error) -> std::optional<size_t> {
    if (argv.size() <= arg_index) {
      if (tabs_.empty()) {
        if (error) *error = "ERR no tabs\n";
        return std::nullopt;
      }
      return active_index_;
    }
    return find_tab_index_arg(argv[arg_index], error);
  };
  auto parse_folder_id = [&](const std::string& text,
                             uint64_t* folder_id) -> bool {
    return ParseUint64Arg(text, folder_id) &&
           SidebarFolderExists(*folder_id);
  };
  if (command == "folder-create") {
    if (argv.size() < 3) {
      return "ERR usage: folder-create <parent-folderid|0> <name>\n";
    }
    uint64_t parent_id = 0;
    if (!parse_folder_id(argv[1], &parent_id)) {
      return "ERR no such parent folder\n";
    }
    if (CreateSidebarFolder(JoinArgs(argv, 2), parent_id) == 0) {
      return "ERR invalid or duplicate folder name\n";
    }
    return FoldersJson();
  }
  if (command == "folder-rename") {
    if (argv.size() < 3) {
      return "ERR usage: folder-rename <folderid> <name>\n";
    }
    uint64_t folder_id = 0;
    if (!parse_folder_id(argv[1], &folder_id) || folder_id == 0) {
      return "ERR no such folder\n";
    }
    if (!RenameSidebarFolder(folder_id, JoinArgs(argv, 2))) {
      return "ERR invalid or duplicate folder name\n";
    }
    return FoldersJson();
  }
  if (command == "folder-delete") {
    if (argv.size() != 3) {
      return "ERR usage: folder-delete <folderid> <recursive|unwrap>\n";
    }
    uint64_t folder_id = 0;
    if (!parse_folder_id(argv[1], &folder_id) || folder_id == 0) {
      return "ERR no such folder\n";
    }
    const std::string mode = ToLowerAscii(argv[2]);
    if (mode != "recursive" && mode != "unwrap") {
      return "ERR usage: folder-delete <folderid> <recursive|unwrap>\n";
    }
    DeleteSidebarFolder(folder_id, mode == "unwrap");
    return FoldersJson();
  }
  if (command == "folder-move") {
    if (argv.size() != 3) {
      return "ERR usage: folder-move <folderid> <parent-folderid|0>\n";
    }
    uint64_t folder_id = 0;
    uint64_t parent_id = 0;
    if (!parse_folder_id(argv[1], &folder_id) || folder_id == 0) {
      return "ERR no such folder\n";
    }
    if (!parse_folder_id(argv[2], &parent_id)) {
      return "ERR no such parent folder\n";
    }
    if (!MoveSidebarItems({{SidebarItemType::kFolder, folder_id}},
                          parent_id)) {
      return "ERR invalid folder move\n";
    }
    return FoldersJson();
  }
  if (command == "tab-folder") {
    if (argv.size() != 3) {
      return "ERR usage: tab-folder <tabid> <folderid|0>\n";
    }
    std::string error;
    const std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
    if (!index) {
      return error;
    }
    uint64_t folder_id = 0;
    if (!parse_folder_id(argv[2], &folder_id)) {
      return "ERR no such folder\n";
    }
    MoveSidebarItems({{SidebarItemType::kTab, tabs_[*index].id}}, folder_id);
    return TabsJson();
  }
  if (command == "folder-pin") {
    if (argv.size() < 2 || argv.size() > 3) {
      return "ERR usage: folder-pin <folderid> [on|off]\n";
    }
    uint64_t folder_id = 0;
    if (!parse_folder_id(argv[1], &folder_id) || folder_id == 0) {
      return "ERR no such folder\n";
    }
    const SidebarFolder* folder = FindSidebarFolder(folder_id);
    bool pinned = !folder->pinned;
    if (argv.size() == 3) {
      const std::string value = ToLowerAscii(argv[2]);
      if (value == "on" || value == "1" || value == "true") {
        pinned = true;
      } else if (value == "off" || value == "0" || value == "false") {
        pinned = false;
      } else {
        return "ERR usage: folder-pin <folderid> [on|off]\n";
      }
    }
    SetFolderPinned(folder_id, pinned);
    return FoldersJson();
  }
  if (command == "tab-pin") {
    if (argv.size() < 2 || argv.size() > 3) {
      return "ERR usage: tab-pin <tabid> [on|off]\n";
    }
    std::string error;
    const std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
    if (!index) {
      return error;
    }
    bool pinned = !tabs_[*index].pinned;
    if (argv.size() == 3) {
      const std::string value = ToLowerAscii(argv[2]);
      if (value == "on" || value == "1" || value == "true") {
        pinned = true;
      } else if (value == "off" || value == "0" || value == "false") {
        pinned = false;
      } else {
        return "ERR usage: tab-pin <tabid> [on|off]\n";
      }
    }
    SetTabPinned(tabs_[*index].id, pinned);
    return TabsJson();
  }
  if (command == "sidebar-folder") {
    if (argv.size() != 2) {
      return "ERR usage: sidebar-folder <folderid|0>\n";
    }
    uint64_t folder_id = 0;
    if (!parse_folder_id(argv[1], &folder_id)) {
      return "ERR no such folder\n";
    }
    if (IsSidebarSearchMode()) {
      CancelCommand();
    }
    sidebar_search_highlights_visible_ = false;
    current_sidebar_folder_id_ = folder_id;
    sidebar_selected_item_ = {};
    sidebar_visual_anchor_ = {};
    EnsureSidebarSelection();
    SaveState();
    if (RefreshSidebar()) {
      Layout();
    }
    return FoldersJson();
  }
  if (command == "sidebar-visibility") {
    if (argv.size() != 2) {
      return "ERR usage: sidebar-visibility <on|off|toggle>\n";
    }
    const std::string value = ToLowerAscii(argv[1]);
    bool visible = sidebar_visible_;
    if (value == "on" || value == "1" || value == "true") {
      visible = true;
    } else if (value == "off" || value == "0" || value == "false") {
      visible = false;
    } else if (value == "toggle") {
      visible = !visible;
    } else {
      return "ERR usage: sidebar-visibility <on|off|toggle>\n";
    }
    if (!visible && mode_ != Mode::kNormal) {
      CancelCommand();
    }
    if (visible != sidebar_visible_) {
      sidebar_visible_ = visible;
      if (!visible && focus_area_ == FocusArea::kTabSidebar) {
        SetFocusArea(FocusArea::kWebView);
      } else {
        RefreshSidebar();
        UpdateModeIndicator();
        Layout();
      }
    }
    return SidebarJson();
  }
  if (command == "sidebar-focus") {
    if (argv.size() > 2) {
      return "ERR usage: sidebar-focus [sidebar|web]\n";
    }
    FocusArea target = FocusArea::kTabSidebar;
    if (argv.size() == 2) {
      const std::string value = ToLowerAscii(argv[1]);
      if (value == "sidebar" || value == "on") {
        target = FocusArea::kTabSidebar;
      } else if (value == "web" || value == "off") {
        target = FocusArea::kWebView;
      } else {
        return "ERR usage: sidebar-focus [sidebar|web]\n";
      }
    }
    if (mode_ != Mode::kNormal) {
      CancelCommand();
    }
    SetFocusArea(target);
    return SidebarJson();
  }
  if (command == "sidebar-select") {
    if (argv.size() < 2 || argv.size() > 3) {
      return "ERR usage: sidebar-select <tab|folder|parent> [id]\n";
    }
    if (IsSidebarSearchMode()) {
      CancelCommand();
    }
    sidebar_search_highlights_visible_ = false;
    const std::string type = ToLowerAscii(argv[1]);
    if (type == "tab") {
      if (argv.size() != 3) {
        return "ERR usage: sidebar-select tab <tabid>\n";
      }
      std::string error;
      const std::optional<size_t> index = find_tab_index_arg(argv[2], &error);
      if (!index) {
        return error;
      }
      current_sidebar_folder_id_ = tabs_[*index].folder_id;
      sidebar_selected_item_ = {SidebarItemType::kTab, tabs_[*index].id};
    } else if (type == "folder") {
      if (argv.size() != 3) {
        return "ERR usage: sidebar-select folder <folderid>\n";
      }
      uint64_t folder_id = 0;
      if (!parse_folder_id(argv[2], &folder_id) || folder_id == 0) {
        return "ERR no such folder\n";
      }
      const SidebarFolder* folder = FindSidebarFolder(folder_id);
      current_sidebar_folder_id_ = folder->parent_id;
      sidebar_selected_item_ = {SidebarItemType::kFolder, folder_id};
    } else if (type == "parent") {
      if (argv.size() != 2) {
        return "ERR usage: sidebar-select parent\n";
      }
      if (current_sidebar_folder_id_ == 0) {
        return "ERR sidebar is already at root\n";
      }
      sidebar_selected_item_ = {SidebarItemType::kParent, 0};
    } else {
      return "ERR usage: sidebar-select <tab|folder|parent> [id]\n";
    }
    sidebar_visual_anchor_ = {};
    sidebar_pending_keys_.clear();
    SaveState();
    RefreshSidebar();
    Layout();
    return SidebarJson();
  }
  if (command == "sidebar-activate") {
    if (argv.size() != 1) {
      return "ERR usage: sidebar-activate\n";
    }
    if (IsSidebarSearchMode()) {
      CommitSidebarSearch();
    }
    ActivateSidebarItem(sidebar_selected_item_);
    return SidebarJson();
  }
  if (command == "sidebar-search") {
    const std::string usage =
        "ERR usage: sidebar-search <forward|backward> <query> | "
        "sidebar-search next [same|opposite|forward|backward] | "
        "sidebar-search clear\n";
    if (argv.size() == 2 && ToLowerAscii(argv[1]) == "clear") {
      if (IsSidebarSearchMode()) {
        CancelCommand();
      }
      ClearSidebarSearchHighlights();
      return SidebarJson();
    }
    if (argv.size() >= 2 && ToLowerAscii(argv[1]) == "next") {
      if (argv.size() > 3 || sidebar_search_query_.empty()) {
        return sidebar_search_query_.empty() ? "ERR no sidebar search\n"
                                             : usage;
      }
      bool forward = sidebar_search_forward_;
      if (argv.size() == 3) {
        const std::string direction = ToLowerAscii(argv[2]);
        if (direction == "opposite") {
          forward = !sidebar_search_forward_;
        } else if (direction == "forward") {
          forward = true;
        } else if (direction == "backward") {
          forward = false;
        } else if (direction != "same") {
          return usage;
        }
      }
      if (!JumpSidebarSearch(forward)) {
        return "ERR no sidebar search match\n";
      }
      return SidebarJson();
    }
    if (argv.size() < 3) {
      return usage;
    }
    const std::string direction = ToLowerAscii(argv[1]);
    const bool forward = direction == "forward";
    if (!forward && direction != "backward") {
      return usage;
    }
    const std::string query = JoinArgs(argv, 2);
    if (query.empty()) {
      return usage;
    }
    if (mode_ != Mode::kNormal) {
      CancelCommand();
    }
    sidebar_search_query_ = query;
    sidebar_search_forward_ = forward;
    sidebar_search_highlights_visible_ = true;
    sidebar_visual_anchor_ = {};
    if (const std::optional<SidebarItemRef> match = FindSidebarSearchMatch(
            query, sidebar_selected_item_, forward)) {
      sidebar_selected_item_ = *match;
    }
    RefreshSidebar();
    Layout();
    return SidebarJson();
  }
  if (command == "tab-focus") {
    if (argv.size() != 2) {
      return "ERR usage: tab-focus <tabid>\n";
    }
    std::string error;
    std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
    if (!index) return error;
    ActivateTab(*index);
    return IpcStatusJson();
  }
  if (command == "tab-delete") {
    if (argv.size() != 2) {
      return "ERR usage: tab-delete <tabid>\n";
    }
    std::string error;
    std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
    if (!index) return error;
    CloseTabAtIndex(*index);
    return IpcStatusJson();
  }
  if (command == "tab-order") {
    if (argv.size() != 3) {
      return "ERR usage: tab-order <tabid> <zero-based-index>\n";
    }
    std::string error;
    std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
    if (!index) return error;
    long target = 0;
    if (!ParseLongArg(argv[2], &target)) {
      return "ERR invalid target index\n";
    }
    if (target < 0) target = 0;
    MoveTabToIndex(*index, static_cast<size_t>(target));
    return TabsJson();
  }
  if (command == "open-tab") {
    if (argv.size() < 2) {
      return "ERR usage: open-tab <url-or-query>\n";
    }
    const std::string text = JoinArgs(argv, 1);
    const std::string url = ResolveUrlOrSearch(text);
    RecordOpenHistory(text);
    AddTab(url, true);
    return IpcStatusJson();
  }
  if (command == "open-context-tab") {
    if (argv.size() < 3) {
      return "ERR usage: open-context-tab <context-name> <url-or-query>\n";
    }
    const std::string text = JoinArgs(argv, 2);
    const std::string url = ResolveUrlOrSearch(text);
    std::string error;
    if (!AddContextTab(argv[1], url, &error)) {
      return error;
    }
    RecordOpenHistory(text);
    return IpcStatusJson();
  }
  if (command == "open") {
    if (argv.size() < 3) {
      return "ERR usage: open <tabid> <url-or-query>\n";
    }
    std::string error;
    std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
    if (!index) return error;
    const std::string text = JoinArgs(argv, 2);
    const std::string url = ResolveUrlOrSearch(text);
    RecordOpenHistory(text);
    const size_t tab_index = *index;
    Tab& tab = tabs_[tab_index];
    last_tab_close_placeholder_ = false;
    SetTabUrl(tab, url);
    if (tab.client && tab.client->browser() &&
        tab.client->browser()->GetMainFrame()) {
      tab.client->browser()->GetMainFrame()->LoadURL(url);
    }
    SaveState();
    RefreshSidebar();
    Layout();
    return TabsJson();
  }
  if (command == "reload" || command == "reload-ignore-cache" ||
      command == "back" || command == "forward" || command == "stop") {
    if (argv.size() > 2) {
      return "ERR usage: reload|reload-ignore-cache|back|forward|stop [tabid]\n";
    }
    std::string error;
    std::optional<size_t> index = tab_index_or_active(1, &error);
    if (!index) return error;
    Tab& tab = tabs_[*index];
    CefRefPtr<CefBrowser> browser = tab.client ? tab.client->browser() : nullptr;
    if (!browser || !browser->GetHost()) {
      return "ERR tab has no browser\n";
    }
    if (command == "reload") {
      browser->Reload();
    } else if (command == "reload-ignore-cache") {
      browser->ReloadIgnoreCache();
    } else if (command == "back") {
      if (browser->CanGoBack()) browser->GoBack();
    } else if (command == "forward") {
      if (browser->CanGoForward()) browser->GoForward();
    } else if (command == "stop") {
      browser->StopLoad();
    }
    return TabsJson();
  }
  if (command == "zoom") {
    if (argv.size() != 2 && argv.size() != 3) {
      return "ERR usage: zoom [tabid] <in|out|reset|level>\n";
    }
    size_t arg = 1;
    std::optional<size_t> index = active_index_;
    if (argv.size() == 3) {
      std::string error;
      index = find_tab_index_arg(argv[1], &error);
      if (!index) return error;
      arg = 2;
    }
    Tab& tab = tabs_[*index];
    CefRefPtr<CefBrowser> browser = tab.client ? tab.client->browser() : nullptr;
    if (!browser || !browser->GetHost()) {
      return "ERR tab has no browser\n";
    }
    const std::string op = ToLowerAscii(argv[arg]);
    if (op == "in" || op == "+") {
      browser->GetHost()->Zoom(CEF_ZOOM_COMMAND_IN);
    } else if (op == "out" || op == "-") {
      browser->GetHost()->Zoom(CEF_ZOOM_COMMAND_OUT);
    } else if (op == "reset" || op == "0") {
      browser->GetHost()->Zoom(CEF_ZOOM_COMMAND_RESET);
    } else {
      double level = 0.0;
      if (!ParseDoubleArg(op, &level)) {
        return "ERR usage: zoom [tabid] <in|out|reset|level>\n";
      }
      browser->GetHost()->SetZoomLevel(level);
    }
    return TabsJson();
  }
  if (command == "scroll-tab") {
    if (argv.size() < 3 || argv.size() > 4) {
      return "ERR usage: scroll-tab <tabid> <dy> [count]\n";
    }
    std::string error;
    std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
    if (!index) return error;
    long dy = 0;
    if (!ParseLongArg(argv[2], &dy)) {
      return "ERR invalid dy\n";
    }
    long count_long = 1;
    if (argv.size() >= 4 && !ParseLongArg(argv[3], &count_long)) {
      return "ERR invalid count\n";
    }
    const int count = std::clamp(static_cast<int>(count_long), 1, 100);
    Tab& tab = tabs_[*index];
    CefRefPtr<CefBrowser> browser = tab.client ? tab.client->browser() : nullptr;
    if (!browser || !browser->GetMainFrame()) {
      return "ERR tab has no browser\n";
    }
    std::ostringstream script;
    script << "window.scrollBy(0," << (dy * count) << ");";
    browser->GetMainFrame()->ExecuteJavaScript(
        script.str(), browser->GetMainFrame()->GetURL(), 0);
    return TabsJson();
  }
  if (command == "showfps") {
    if (argv.size() == 1) {
      SetShowFpsIndicator(!show_fps_indicator_);
      return IpcStatusJson();
    }
    const std::string arg = ToLowerAscii(argv[1]);
    if (arg == "on" || arg == "1" || arg == "true") {
      SetShowFpsIndicator(true);
      return IpcStatusJson();
    }
    if (arg == "off" || arg == "0" || arg == "false") {
      SetShowFpsIndicator(false);
      return IpcStatusJson();
    }
    return "ERR usage: showfps [on|off]\n";
  }
  if (command == "showstatusline") {
    if (argv.size() == 1) {
      SetShowStatusLine(!show_statusline_);
      return IpcStatusJson();
    }
    const std::string arg = ToLowerAscii(argv[1]);
    if (arg == "on" || arg == "1" || arg == "true") {
      SetShowStatusLine(true);
      return IpcStatusJson();
    }
    if (arg == "off" || arg == "0" || arg == "false") {
      SetShowStatusLine(false);
      return IpcStatusJson();
    }
    return "ERR usage: showstatusline [on|off]\n";
  }
  if (command == "shader") {
    if (argv.size() == 1) {
      SetShaderEnabled(!shader_enabled_);
      return IpcStatusJson();
    }
    const std::string arg = ToLowerAscii(argv[1]);
    if (arg == "on" || arg == "1" || arg == "true") {
      SetShaderEnabled(true);
      return IpcStatusJson();
    }
    if (arg == "off" || arg == "0" || arg == "false") {
      SetShaderEnabled(false);
      return IpcStatusJson();
    }
    return "ERR usage: shader [on|off]\n";
  }
  if (command == "scroll") {
    if (argv.size() < 2) {
      return "ERR usage: scroll <dy> [count]\n";
    }
    char* end = nullptr;
    const long dy = std::strtol(argv[1].c_str(), &end, 10);
    if (end == argv[1].c_str()) {
      return "ERR invalid dy\n";
    }
    int count = 1;
    if (argv.size() >= 3) {
      char* count_end = nullptr;
      count = static_cast<int>(std::strtol(argv[2].c_str(), &count_end, 10));
      if (count_end == argv[2].c_str()) {
        return "ERR invalid count\n";
      }
    }
    count = std::clamp(count, 1, 100);
    for (int i = 0; i < count; ++i) {
      ScrollActivePageBy(static_cast<int>(dy));
    }
    return IpcStatusJson();
  }
  if (command == "tab") {
    if (argv.size() != 2) {
      return "ERR usage: tab <1-based-index>\n";
    }
    char* end = nullptr;
    const long index = std::strtol(argv[1].c_str(), &end, 10);
    if (end == argv[1].c_str() || index <= 0) {
      return "ERR invalid tab index\n";
    }
    ActivateTab(static_cast<size_t>(index - 1));
    return IpcStatusJson();
  }
  if (command == "tab-close") {
    if (argv.size() == 1) {
      CloseActiveTab();
      return IpcStatusJson();
    }
    if (argv.size() == 2) {
      std::string error;
      std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
      if (!index) return error;
      CloseTabAtIndex(*index);
      return IpcStatusJson();
    }
    return "ERR usage: tab-close [tabid]\n";
  }
  if (command == "help") {
    return "commands:\n"
           "  version|protocol\n"
           "  status|json\n"
           "  tabs\n"
           "  commands\n"
           "  folders\n"
           "  folder-create <parent-folderid|0> <name>\n"
           "  folder-rename <folderid> <name>\n"
           "  folder-delete <folderid> <recursive|unwrap>\n"
           "  folder-move <folderid> <parent-folderid|0>\n"
           "  folder-pin <folderid> [on|off]\n"
           "  tab-folder <tabid> <folderid|0>\n"
           "  tab-pin <tabid> [on|off]\n"
           "  sidebar\n"
           "  sidebar-folder <folderid|0>\n"
           "  sidebar-visibility <on|off|toggle>\n"
           "  sidebar-focus [sidebar|web]\n"
           "  sidebar-select <tab|folder|parent> [id]\n"
           "  sidebar-activate\n"
           "  sidebar-search <forward|backward> <query>\n"
           "  sidebar-search next [same|opposite|forward|backward]\n"
           "  sidebar-search clear\n"
           "  tab-focus <tabid>\n"
           "  tab-delete <tabid>\n"
           "  tab-order <tabid> <zero-based-index>\n"
           "  open-tab <url-or-query>\n"
           "  open-context-tab <context-name> <url-or-query>\n"
           "  open <tabid> <url-or-query>\n"
           "  reload [tabid]\n"
           "  reload-ignore-cache [tabid]\n"
           "  back [tabid]\n"
           "  forward [tabid]\n"
           "  stop [tabid]\n"
           "  zoom [tabid] <in|out|reset|level>\n"
           "  scroll <dy> [count]\n"
           "  scroll-tab <tabid> <dy> [count]\n"
           "  html <tabid>\n"
           "  text <tabid>\n"
           "  screenshot <tabid>\n"
           "  js <tabid> <javascript>\n"
           "  js-file <tabid> <path>\n"
           "  cookies <tabid> [url]\n"
           "  cookies-url <url>\n"
           "  cookie-delete <tabid> <name>\n"
           "  cookie-set <tabid> <name> <value> [domain] [path]\n"
           "  network <tabid> list\n"
           "  network <tabid> detail <requestid>\n"
           "  network <tabid> body <requestid>\n"
           "  network <tabid> replay <requestid>\n"
           "  network <tabid> clear\n"
           "  fps\n"
           "  refresh\n"
           "  url\n"
           "  showfps [on|off]\n"
           "  showstatusline [on|off]\n"
           "  shader [on|off]\n"
           "  tab <1-based-index>\n"
           "  tab-close [tabid]\n";
  }
  return "ERR unknown command\n";
}

void BrowserWindow::HandleIpcCommandAsync(const std::string& command_line,
                                          IpcReplyCallback reply) {
  const std::vector<std::string> argv = SplitArgs(command_line);
  if (argv.empty()) {
    reply("ERR empty command\n");
    return;
  }

  const std::string command = ToLowerAscii(argv[0]);
  auto parse_tab_id = [&](size_t arg_index, uint64_t* tab_id) -> bool {
    if (argv.size() <= arg_index ||
        !ParseUint64Arg(argv[arg_index], tab_id) || *tab_id == 0) {
      reply("ERR invalid tabid\n");
      return false;
    }
    if (!FindTabIndexById(*tab_id)) {
      reply("ERR no such tabid\n");
      return false;
    }
    return true;
  };

  if (command == "html" || command == "text") {
    if (argv.size() != 2) {
      reply("ERR usage: html|text <tabid>\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    HandleHtmlIpcCommand(tab_id, command == "text", std::move(reply));
    return;
  }

  if (command == "screenshot") {
    if (argv.size() != 2) {
      reply("ERR usage: screenshot <tabid>\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    HandleScreenshotIpcCommand(tab_id, std::move(reply));
    return;
  }

  if (command == "js") {
    if (argv.size() < 3) {
      reply("ERR usage: js <tabid> <javascript>\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    HandleJsIpcCommand(tab_id, JoinArgs(argv, 2), std::move(reply));
    return;
  }

  if (command == "js-file") {
    if (argv.size() != 3) {
      reply("ERR usage: js-file <tabid> <path>\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    std::string error;
    std::string code = ReadFileToString(argv[2], &error);
    if (!error.empty()) {
      reply(error);
      return;
    }
    HandleJsIpcCommand(tab_id, std::move(code), std::move(reply));
    return;
  }

  if (command == "cookies") {
    if (argv.size() < 2) {
      reply("ERR usage: cookies <tabid> [url]\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    HandleCookiesIpcCommand(tab_id,
                            argv.size() >= 3 ? JoinArgs(argv, 2) : std::string(),
                            std::move(reply));
    return;
  }

  if (command == "cookies-url") {
    if (argv.size() < 2) {
      reply("ERR usage: cookies-url <url>\n");
      return;
    }
    HandleCookiesForUrlIpcCommand(JoinArgs(argv, 1), std::move(reply));
    return;
  }

  if (command == "cookie-delete") {
    if (argv.size() != 3) {
      reply("ERR usage: cookie-delete <tabid> <name>\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    HandleCookieDeleteIpcCommand(tab_id, argv[2], std::move(reply));
    return;
  }

  if (command == "cookie-set") {
    if (argv.size() < 4 || argv.size() > 6) {
      reply("ERR usage: cookie-set <tabid> <name> <value> [domain] [path]\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    HandleCookieSetIpcCommand(tab_id, argv[2], argv[3],
                              argv.size() >= 5 ? argv[4] : std::string(),
                              argv.size() >= 6 ? argv[5] : std::string(),
                              std::move(reply));
    return;
  }

  if (command == "network") {
    if (argv.size() < 3) {
      reply("ERR usage: network <tabid> list|detail|body|replay [requestid]\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    std::optional<size_t> index = FindTabIndexById(tab_id);
    if (!index || !tabs_[*index].client) {
      reply("ERR tab has no client\n");
      return;
    }
    const std::string subcommand = ToLowerAscii(argv[2]);
    if (subcommand == "list" || subcommand == "clear") {
      if (argv.size() != 3) {
        reply("ERR usage: network <tabid> list|clear\n");
        return;
      }
      if (subcommand == "clear") {
        tabs_[*index].client->ClearNetworkLog();
        reply("{\"cleared\":true}");
        return;
      }
      reply(tabs_[*index].client->NetworkListJson());
      return;
    }
    if (subcommand == "detail" || subcommand == "body" || subcommand == "replay") {
      if (argv.size() != 4) {
        reply("ERR usage: network <tabid> detail|body|replay <requestid>\n");
        return;
      }
      uint64_t request_id = 0;
      if (!ParseUint64Arg(argv[3], &request_id) || request_id == 0) {
        reply("ERR invalid requestid\n");
        return;
      }
      if (subcommand == "detail") {
        reply(tabs_[*index].client->NetworkDetailJson(request_id));
        return;
      }
      if (subcommand == "body") {
        std::string body;
        std::string error;
        if (!tabs_[*index].client->NetworkBody(request_id, &body, &error)) {
          reply(error);
          return;
        }
        reply(std::move(body));
        return;
      }
      HandleNetworkReplayIpcCommand(tab_id, request_id, std::move(reply));
      return;
    }
    reply("ERR usage: network <tabid> list|detail|body|replay [requestid]\n");
    return;
  }

  reply(HandleIpcCommand(command_line));
}

std::string BrowserWindow::IpcStatusJson() const {
  bool fps_has_sample = false;
  double fps = 0.0;
  double refresh_rate = 0.0;
  std::string url;
  std::string title;
  std::string context;
  bool audible = false;
  if (!tabs_.empty() && active_index_ < tabs_.size()) {
    const Tab& tab = tabs_[active_index_];
    url = tab.url;
    context = tab.context;
    audible = tab.audible;
    if (tab.client) {
      fps_has_sample = tab.client->fps_has_sample();
      fps = tab.client->current_fps();
      refresh_rate = tab.client->compositor_refresh_rate();
    }
    if (CefRefPtr<CefBrowser> browser = tab.client ? tab.client->browser() : nullptr;
        browser && browser->GetHost()) {
      CefRefPtr<CefNavigationEntry> entry = browser->GetHost()->GetVisibleNavigationEntry();
      if (entry) {
        title = entry->GetTitle().ToString();
      }
    }
  }

  std::string out;
  out.reserve(256 + url.size() + title.size());
  out += "{\"ipc_protocol\":\"";
  out += kIpcProtocolName;
  out += "\",\"ipc_version\":";
  AppendJsonNumber(out, kIpcProtocolVersion);
  out += ",\"active_tabid\":";
  AppendJsonNumber(out, ActiveTabId());
  out += ",\"active_index\":";
  AppendJsonNumber(out, active_index_);
  out += ",\"active_tab\":";
  AppendJsonNumber(out, active_index_ + 1);
  out += ",\"tabs\":";
  AppendJsonNumber(out, tabs_.size());
  out += ",\"current_folder_id\":";
  AppendJsonNumber(out, current_sidebar_folder_id_);
  out += ",\"sidebar_selected_type\":";
  switch (sidebar_selected_item_.type) {
    case SidebarItemType::kParent:
      AppendJsonString(out, "parent");
      break;
    case SidebarItemType::kFolder:
      AppendJsonString(out, "folder");
      break;
    case SidebarItemType::kTab:
      AppendJsonString(out, "tab");
      break;
    case SidebarItemType::kNone:
      AppendJsonString(out, "none");
      break;
  }
  out += ",\"sidebar_selected_id\":";
  AppendJsonNumber(out, sidebar_selected_item_.id);
  out += ",\"url\":";
  AppendJsonString(out, url);
  out += ",\"title\":";
  AppendJsonString(out, title);
  out += ",\"context\":";
  if (context.empty()) {
    out += "null";
  } else {
    AppendJsonString(out, context);
  }
  out += ",\"audible\":";
  AppendJsonBool(out, audible);
  out += ",\"showfps\":";
  AppendJsonBool(out, show_fps_indicator_);
  out += ",\"showstatusline\":";
  AppendJsonBool(out, show_statusline_);
  out += ",\"shader\":";
  AppendJsonBool(out, shader_enabled_);
  out += ",\"mode\":";
  AppendJsonString(out, ModeIndicatorText());
  out += ",\"fps_has_sample\":";
  AppendJsonBool(out, fps_has_sample);
  out += ",\"fps\":";
  if (fps_has_sample) {
    AppendJsonNumber(out, static_cast<int>(std::round(fps)));
  } else {
    out += "null";
  }
  out += ",\"refresh_rate\":";
  AppendJsonNumber(out, refresh_rate);
  out += "}";
  return out;
}


}  // namespace vimbrowser
