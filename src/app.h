#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"
#include "include/cef_render_process_handler.h"

namespace vimbrowser {

class App final : public CefApp,
                  public CefBrowserProcessHandler,
                  public CefRenderProcessHandler {
 public:
  App(std::vector<std::string> initial_urls,
      std::vector<uint64_t> initial_tab_folder_ids,
      std::vector<uint64_t> initial_tab_sort_orders,
      std::vector<bool> initial_tab_pinned,
      size_t active_index,
      bool show_mode_indicator,
      bool show_fps_indicator,
      bool show_statusline,
      bool shader_enabled,
      std::string state_path,
      std::string dwm_save_argv,
      std::string root_cache_path,
      bool disable_gpu,
      bool a26_shell);

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }

  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;
  bool OnAlreadyRunningAppRelaunch(
      CefRefPtr<CefCommandLine> command_line,
      const CefString& current_directory) override;
  void OnContextInitialized() override;
  void OnFocusedNodeChanged(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefDOMNode> node) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

 private:
  std::vector<std::string> initial_urls_;
  std::vector<uint64_t> initial_tab_folder_ids_;
  std::vector<uint64_t> initial_tab_sort_orders_;
  std::vector<bool> initial_tab_pinned_;
  size_t active_index_;
  bool show_mode_indicator_;
  bool show_fps_indicator_;
  bool show_statusline_;
  bool shader_enabled_;
  std::string state_path_;
  std::string dwm_save_argv_;
  std::string root_cache_path_;
  bool disable_gpu_;
  bool a26_shell_;

  IMPLEMENT_REFCOUNTING(App);
  DISALLOW_COPY_AND_ASSIGN(App);
};

}  // namespace vimbrowser
