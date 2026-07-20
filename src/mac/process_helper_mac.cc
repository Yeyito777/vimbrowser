// Entry point for the vimbrowser Helper apps (renderer / GPU / plugin /
// alerts subprocesses) on macOS. Linux runs subprocesses through the main
// executable, but macOS requires distinct helper app bundles.
//
// The full vimbrowser::App is instantiated so renderer processes get the same
// CefRenderProcessHandler (focused-editable tracking, IPC JS eval) as the
// Linux single-binary build. Browser-process-only constructor arguments are
// irrelevant in subprocesses.
#include "app.h"
#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"

int main(int argc, char* argv[]) {
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInHelper()) {
    return 1;
  }

  CefMainArgs main_args(argc, argv);
  CefRefPtr<vimbrowser::App> app(new vimbrowser::App(
      /*initial_urls=*/{}, /*initial_tab_folder_ids=*/{},
      /*initial_tab_sort_orders=*/{}, /*initial_tab_pinned=*/{},
      /*active_index=*/0, /*show_mode_indicator=*/false,
      /*show_fps_indicator=*/false, /*show_statusline=*/false,
      /*shader_enabled=*/false, /*state_path=*/{}, /*dwm_save_argv=*/{},
      /*root_cache_path=*/{}, /*disable_gpu=*/false, /*a26_shell=*/false));
  return CefExecuteProcess(main_args, app, nullptr);
}
