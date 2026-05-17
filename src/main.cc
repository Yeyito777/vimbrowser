#include <iostream>
#include <limits.h>
#include <unistd.h>

#include "app.h"
#include "config.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"

namespace {

void SetCefString(cef_string_t* target, const std::string& value) {
  CefString(target).FromString(value);
}

std::string ExecutablePath() {
  char path[PATH_MAX];
  const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (length <= 0) {
    return {};
  }
  path[length] = '\0';
  return path;
}

std::string Dirname(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return ".";
  }
  return path.substr(0, slash);
}

}  // namespace

int main(int argc, char* argv[]) {
  const std::string exe_path = ExecutablePath();
  const std::string exe_dir = Dirname(exe_path);
  if (!exe_dir.empty()) {
    // CEF subprocess startup needs to find ICU/resources immediately. Keep the
    // shell robust when launched via ~/.local/bin/vimbrowser or any other cwd.
    if (chdir(exe_dir.c_str()) != 0) {
      std::cerr << "vimbrowser: warning: failed to chdir to " << exe_dir
                << std::endl;
    }
  }

  CefMainArgs main_args(argc, argv);
  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromArgv(argc, argv);

  vimbrowser::Config config = vimbrowser::ParseConfig(argc, argv);
  CefRefPtr<vimbrowser::App> app(new vimbrowser::App(config.initial_urls,
                                                     config.active_index,
                                                     config.show_mode_indicator,
                                                     config.show_fps_indicator,
                                                     config.state_path,
                                                     config.disable_gpu));

  const int sub_process_exit_code = CefExecuteProcess(main_args, app, nullptr);
  if (sub_process_exit_code >= 0) {
    return sub_process_exit_code;
  }

  CefSettings settings;
  settings.no_sandbox = true;
  settings.remote_debugging_port = config.remote_debugging_port;
  settings.persist_session_cookies = true;
  settings.log_severity = LOGSEVERITY_WARNING;
  SetCefString(&settings.root_cache_path, config.cache_path);
  SetCefString(&settings.cache_path, config.cache_path + "/default");

  if (!exe_path.empty()) {
    SetCefString(&settings.browser_subprocess_path, exe_path);
  }
  if (!exe_dir.empty()) {
    SetCefString(&settings.resources_dir_path, exe_dir);
    SetCefString(&settings.locales_dir_path, exe_dir + "/locales");
  }

  if (!CefInitialize(main_args, settings, app, nullptr)) {
    std::cerr << "vimbrowser: CefInitialize failed" << std::endl;
    return 1;
  }

  std::cout << "vimbrowser: " << config.initial_url << std::endl;
  if (config.explicit_profile_dir) {
    std::cout << "vimbrowser: profile " << config.profile_dir << std::endl;
  }
  std::cout << "vimbrowser: state " << config.state_path << std::endl;
  std::cout << "vimbrowser: cef " << config.cache_path << std::endl;
  if (config.remote_debugging_port > 0) {
    std::cout << "vimbrowser: CDP http://127.0.0.1:"
              << config.remote_debugging_port << std::endl;
  }

  CefRunMessageLoop();
  CefShutdown();
  return 0;
}
