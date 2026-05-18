#pragma once

#include <string>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"

namespace vimbrowser {

class App final : public CefApp, public CefBrowserProcessHandler {
 public:
  App(std::vector<std::string> initial_urls,
      size_t active_index,
      bool show_mode_indicator,
      bool show_fps_indicator,
      std::string state_path,
      bool disable_gpu);

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;
  bool OnAlreadyRunningAppRelaunch(
      CefRefPtr<CefCommandLine> command_line,
      const CefString& current_directory) override;
  void OnContextInitialized() override;

 private:
  std::vector<std::string> initial_urls_;
  size_t active_index_;
  bool show_mode_indicator_;
  bool show_fps_indicator_;
  std::string state_path_;
  bool disable_gpu_;

  IMPLEMENT_REFCOUNTING(App);
  DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace vimbrowser
