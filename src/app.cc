#include "app.h"

#include <cmath>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include "browser_window.h"
#include "include/cef_process_message.h"
#include "include/cef_values.h"
#include "include/cef_v8.h"

namespace vimbrowser {
namespace {

constexpr const char kJsEvalMessage[] = "__vimbrowser_ipc_js_eval__";
constexpr const char kJsResultMessage[] = "__vimbrowser_ipc_js_result__";

std::string JsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          constexpr char kHex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(kHex[(c >> 4) & 0xf]);
          out.push_back(kHex[c & 0xf]);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

std::string V8ValueToJsonValue(CefRefPtr<CefV8Value> value, int depth) {
  if (!value || !value->IsValid()) {
    return "null";
  }
  if (value->IsUndefined()) {
    return "null";
  }
  if (value->IsNull()) {
    return "null";
  }
  if (value->IsBool()) {
    return value->GetBoolValue() ? "true" : "false";
  }
  if (value->IsInt()) {
    return std::to_string(value->GetIntValue());
  }
  if (value->IsUInt()) {
    return std::to_string(value->GetUIntValue());
  }
  if (value->IsDouble()) {
    const double number = value->GetDoubleValue();
    if (!std::isfinite(number)) {
      return "null";
    }
    std::ostringstream out;
    out << number;
    return out.str();
  }
  if (value->IsString()) {
    return "\"" + JsonEscape(value->GetStringValue().ToString()) + "\"";
  }
  if (value->IsFunction()) {
    return "\"[function " + JsonEscape(value->GetFunctionName().ToString()) + "]\"";
  }
  if (value->IsPromise()) {
    return "\"[promise]\"";
  }
  if (value->IsArray()) {
    if (depth <= 0) {
      return "\"[array]\"";
    }
    std::ostringstream out;
    out << "[";
    const int length = std::min(value->GetArrayLength(), 100);
    for (int i = 0; i < length; ++i) {
      if (i) {
        out << ",";
      }
      out << V8ValueToJsonValue(value->GetValue(i), depth - 1);
    }
    out << "]";
    return out.str();
  }
  if (value->IsObject()) {
    if (depth <= 0) {
      return "\"[object]\"";
    }
    std::vector<CefString> keys;
    if (!value->GetKeys(keys)) {
      return "\"[object]\"";
    }
    std::ostringstream out;
    out << "{";
    size_t written = 0;
    for (const CefString& key : keys) {
      if (written >= 100) {
        break;
      }
      CefRefPtr<CefV8Value> child = value->GetValue(key);
      if (!child) {
        continue;
      }
      if (written) {
        out << ",";
      }
      out << "\"" << JsonEscape(key.ToString()) << "\":"
          << V8ValueToJsonValue(child, depth - 1);
      ++written;
    }
    out << "}";
    return out.str();
  }
  return "null";
}

std::string V8ValueType(CefRefPtr<CefV8Value> value) {
  if (!value) return "null";
  if (value->IsUndefined()) return "undefined";
  if (value->IsNull()) return "null";
  if (value->IsBool()) return "boolean";
  if (value->IsInt() || value->IsUInt() || value->IsDouble()) return "number";
  if (value->IsString()) return "string";
  if (value->IsArray()) return "array";
  if (value->IsFunction()) return "function";
  if (value->IsPromise()) return "promise";
  if (value->IsObject()) return "object";
  return "unknown";
}

std::string EvalJsForIpc(CefRefPtr<CefFrame> frame, const std::string& code) {
  if (!frame || !frame->IsValid()) {
    return R"JSON({"ok":false,"error":"invalid frame"})JSON";
  }
  CefRefPtr<CefV8Context> context = frame->GetV8Context();
  if (!context || !context->IsValid()) {
    return R"JSON({"ok":false,"error":"no valid V8 context"})JSON";
  }
  if (!context->Enter()) {
    return R"JSON({"ok":false,"error":"failed to enter V8 context"})JSON";
  }

  CefRefPtr<CefV8Value> retval;
  CefRefPtr<CefV8Exception> exception;
  const bool ok = context->Eval(code, frame->GetURL(), 1, retval, exception);
  context->Exit();

  if (!ok) {
    std::ostringstream out;
    out << "{\"ok\":false,\"error\":\""
        << JsonEscape(exception ? exception->GetMessage().ToString()
                                : std::string("evaluation failed"))
        << "\"";
    if (exception) {
      out << ",\"line\":" << exception->GetLineNumber()
          << ",\"source\":\"" << JsonEscape(exception->GetSourceLine().ToString())
          << "\"";
    }
    out << "}";
    return out.str();
  }

  std::ostringstream out;
  out << "{\"ok\":true,\"type\":\"" << V8ValueType(retval)
      << "\",\"result\":" << V8ValueToJsonValue(retval, 3);
  if (retval && retval->IsPromise()) {
    out << ",\"promise\":\"not awaited; IPC JS eval is currently synchronous\"";
  }
  out << "}";
  return out.str();
}

}  // namespace

App::App(std::vector<std::string> initial_urls,
         size_t active_index,
         bool show_mode_indicator,
         bool show_fps_indicator,
         bool shader_enabled,
         std::string state_path,
         bool disable_gpu)
    : initial_urls_(std::move(initial_urls)),
      active_index_(active_index),
      show_mode_indicator_(show_mode_indicator),
      show_fps_indicator_(show_fps_indicator),
      shader_enabled_(shader_enabled),
      state_path_(std::move(state_path)),
      disable_gpu_(disable_gpu) {}

void App::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  // These are public vimbrowser-level flags. Do not forward them into Chromium's
  // command line or process-singleton relaunch messages.
  command_line->RemoveSwitch("profile-dir");
  command_line->RemoveSwitch("cache-path");

  // Keep the shell minimal and deterministic. These are Chromium switches, not
  // external UI toolkits.
  command_line->AppendSwitch("disable-extensions");
  command_line->AppendSwitch("disable-background-networking");
  command_line->AppendSwitch("disable-sync");
  command_line->AppendSwitch("no-default-browser-check");
  command_line->AppendSwitchWithValue("disable-features", "Translate,MediaRouter");
  if (!command_line->HasSwitch("autoplay-policy")) {
    // Desktop Chromium's default is no-user-gesture-required. vimbrowser should
    // not let restored tabs or freshly loaded pages start media on their own;
    // explicit user play/click gestures still work.
    command_line->AppendSwitchWithValue("autoplay-policy",
                                       "user-gesture-required");
  }

  if (disable_gpu_) {
    command_line->AppendSwitch("disable-gpu");
  }
}

bool App::OnAlreadyRunningAppRelaunch(
    CefRefPtr<CefCommandLine>,
    const CefString&) {
  // A durable --profile-dir is intentionally single-writer. For now, acknowledge
  // relaunches so CEF exits the second process cleanly instead of creating a
  // default Chrome-styled window against our profile. Opening URLs in the
  // existing vimbrowser process should go through our Unix IPC protocol.
  return true;
}

void App::OnContextInitialized() {
  CefRefPtr<BrowserWindow> window(new BrowserWindow(initial_urls_, active_index_,
                                                    show_mode_indicator_,
                                                    show_fps_indicator_,
                                                    shader_enabled_, state_path_));
  window->Create();
}

bool App::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefProcessId source_process,
                                   CefRefPtr<CefProcessMessage> message) {
  if (!message || message->GetName().ToString() != kJsEvalMessage) {
    return false;
  }
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  const std::string request_id = args && args->GetSize() >= 1
                                     ? args->GetString(0).ToString()
                                     : std::string();
  const std::string code = args && args->GetSize() >= 2
                               ? args->GetString(1).ToString()
                               : std::string();
  const std::string payload = EvalJsForIpc(frame, code);

  CefRefPtr<CefProcessMessage> response = CefProcessMessage::Create(kJsResultMessage);
  CefRefPtr<CefListValue> response_args = response->GetArgumentList();
  response_args->SetString(0, request_id);
  response_args->SetString(1, payload);
  if (frame && frame->IsValid()) {
    frame->SendProcessMessage(PID_BROWSER, response);
  }
  return true;
}

}  // namespace vimbrowser
