#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits.h>
#include <string>
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

bool IsPidAlive(int pid) {
  if (pid <= 0) {
    return false;
  }
  if (kill(pid, 0) == 0) {
    return true;
  }
  return errno == EPERM;
}

int PidFromChromeSingletonLock(const std::filesystem::path& lock_path) {
  std::error_code ec;
  if (!std::filesystem::is_symlink(lock_path, ec)) {
    return -1;
  }
  const std::string target = std::filesystem::read_symlink(lock_path, ec).string();
  if (ec || target.empty()) {
    return -1;
  }
  const size_t delimiter = target.rfind('-');
  if (delimiter == std::string::npos || delimiter + 1 >= target.size()) {
    return -1;
  }
  const std::string pid_text = target.substr(delimiter + 1);
  char* end = nullptr;
  errno = 0;
  const long pid = std::strtol(pid_text.c_str(), &end, 10);
  if (end == pid_text.c_str() || *end != '\0' || errno != 0 || pid <= 0) {
    return -1;
  }
  return static_cast<int>(pid);
}

void RemoveIfExists(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

void CleanStaleChromeSingleton(const std::string& root_cache_path) {
  if (root_cache_path.empty()) {
    return;
  }

  const std::filesystem::path root(root_cache_path);
  const std::filesystem::path lock = root / "SingletonLock";
  const std::filesystem::path socket = root / "SingletonSocket";
  const std::filesystem::path cookie = root / "SingletonCookie";

  std::error_code ec;
  const bool has_lock = std::filesystem::exists(
      std::filesystem::symlink_status(lock, ec));
  ec.clear();
  const bool has_socket = std::filesystem::exists(
      std::filesystem::symlink_status(socket, ec));
  ec.clear();
  const bool has_cookie = std::filesystem::exists(
      std::filesystem::symlink_status(cookie, ec));
  if (!has_lock && !has_socket && !has_cookie) {
    return;
  }

  const int pid = PidFromChromeSingletonLock(lock);
  if (IsPidAlive(pid)) {
    return;
  }

  // Chromium's process singleton can abort on Linux if a stale SingletonSocket
  // accepts and immediately resets the relaunch connection while the matching
  // SingletonLock is missing or points at a dead process. Remove only the three
  // singleton coordination symlinks/files; keep all profile data intact.
  RemoveIfExists(socket);
  RemoveIfExists(cookie);
  RemoveIfExists(lock);
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

  CleanStaleChromeSingleton(config.cache_path);

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
    const int exit_code = CefGetExitCode();
    if (exit_code == CEF_RESULT_CODE_NORMAL_EXIT ||
        exit_code == CEF_RESULT_CODE_NORMAL_EXIT_PROCESS_NOTIFIED) {
      return 0;
    }
    std::cerr << "vimbrowser: CefInitialize failed with exit code "
              << exit_code << std::endl;
    return exit_code == 0 ? 1 : exit_code;
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
