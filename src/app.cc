#include "app.h"

#include <utility>

#include "browser_window.h"
#include "element_shader_js.h"
#include "include/cef_process_message.h"

namespace vimbrowser {

App::App(std::string initial_url, bool disable_gpu)
    : initial_url_(std::move(initial_url)), disable_gpu_(disable_gpu) {}

void App::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  // Keep the shell minimal and deterministic. These are Chromium switches, not
  // external UI toolkits.
  command_line->AppendSwitch("disable-extensions");
  command_line->AppendSwitch("disable-background-networking");
  command_line->AppendSwitch("disable-sync");
  command_line->AppendSwitch("no-default-browser-check");
  command_line->AppendSwitchWithValue("disable-features", "Translate,MediaRouter");

  if (disable_gpu_) {
    command_line->AppendSwitch("disable-gpu");
  }
}

void App::OnContextInitialized() {
  CefRefPtr<BrowserWindow> window(new BrowserWindow(initial_url_));
  window->Create();
}

namespace {

std::string ElementShaderScriptForRenderer(bool enabled) {
  std::string script = ElementShaderScript();
  const std::string placeholder = "__VIMBROWSER_SHADER_ENABLED__";
  const size_t pos = script.find(placeholder);
  if (pos != std::string::npos) {
    script.replace(pos, placeholder.size(), enabled ? "true" : "false");
  }
  return script;
}

}  // namespace

void App::OnContextCreated(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefV8Context> context) {
  if (!frame || !frame->IsMain()) {
    return;
  }
  CefRefPtr<CefV8Value> global = context->GetGlobal();
  if (global) {
    global->SetValue("__vimbrowserShaderReady",
                     CefV8Value::CreateFunction("__vimbrowserShaderReady", this),
                     V8_PROPERTY_ATTRIBUTE_NONE);
  }
  // This runs in the renderer as soon as the page's V8 context exists, before
  // BrowserWindow gets load-end callbacks. The script installs a MutationObserver
  // so it can attach the shader style as soon as <html>/<head> exist, preventing
  // the visible unshaded flash caused by browser-process load-end injection.
  frame->ExecuteJavaScript(ElementShaderScriptForRenderer(renderer_shader_enabled_),
                           frame->GetURL(), 0);
}

bool App::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefProcessId source_process,
                                   CefRefPtr<CefProcessMessage> message) {
  if (!message || message->GetName() != "vimbrowser:set-shader") {
    return false;
  }
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  renderer_shader_enabled_ = args && args->GetSize() > 0 && args->GetBool(0);
  return true;
}

bool App::Execute(const CefString& name,
                  CefRefPtr<CefV8Value> object,
                  const CefV8ValueList& arguments,
                  CefRefPtr<CefV8Value>& retval,
                  CefString& exception) {
  if (name != "__vimbrowserShaderReady") {
    return false;
  }
  CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
  if (context && context->GetFrame()) {
    CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create("vimbrowser:shader-ready");
    context->GetFrame()->SendProcessMessage(PID_BROWSER, message);
  }
  return true;
}

}  // namespace vimbrowser
