#pragma once

#include <atomic>
#include <string>
#include <thread>

namespace vimbrowser {

class BrowserWindow;

// Canonical vimbrowser application IPC transport.
//
// This Unix-domain socket is the supported local automation/control interface for
// vimbrowser chrome and app state. Prefer extending this protocol over adding
// ad-hoc diagnostics, log scraping, or DevTools/Chrome-specific control paths.
// Protocol and compatibility rules live in docs/ipc.md.
class IpcServer final {
 public:
  IpcServer(BrowserWindow* owner, std::string socket_path);
  ~IpcServer();

  bool Start();
  void Stop();
  const std::string& socket_path() const { return socket_path_; }

 private:
  void Loop();
  void HandleClient(int client_fd);

  BrowserWindow* owner_ = nullptr;
  std::string socket_path_;
  std::atomic<bool> running_{false};
  int server_fd_ = -1;
  std::thread thread_;
};

}  // namespace vimbrowser
