#include "app.h"

#include <utility>

#include "browser_window.h"

namespace vimbrowser {

App::App(std::vector<std::string> initial_urls,
         size_t active_index,
         bool show_mode_indicator,
         bool show_fps_indicator,
         std::string state_path,
         bool disable_gpu)
    : initial_urls_(std::move(initial_urls)),
      active_index_(active_index),
      show_mode_indicator_(show_mode_indicator),
      show_fps_indicator_(show_fps_indicator),
      state_path_(std::move(state_path)),
      disable_gpu_(disable_gpu) {}

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
  CefRefPtr<BrowserWindow> window(new BrowserWindow(initial_urls_, active_index_,
                                                    show_mode_indicator_,
                                                    show_fps_indicator_, state_path_));
  window->Create();
}

}  // namespace vimbrowser
