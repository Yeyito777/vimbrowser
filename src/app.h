#pragma once

#include <string>

#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"

namespace vimbrowser {

class App final : public CefApp, public CefBrowserProcessHandler {
 public:
  App(std::string initial_url, bool disable_gpu);

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;
  void OnContextInitialized() override;

 private:
  std::string initial_url_;
  bool disable_gpu_;

  IMPLEMENT_REFCOUNTING(App);
  DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace vimbrowser
