#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <deque>

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
  void HealthLoop();

  BrowserWindow* owner_ = nullptr;
  std::string socket_path_;
  std::atomic<bool> running_{false};
  int server_fd_ = -1;
  std::atomic<int> client_fd_{-1};
  std::mutex pending_command_mutex_;
  std::function<void()> cancel_pending_command_;
  std::thread thread_;
  int health_fd_ = -1;
  std::thread health_thread_;
  // Snapshot only: the health thread never touches CEF/UI objects.
  std::deque<std::function<std::string()>> operation_snapshots_;
  uint64_t operation_sequence_ = 0;
};

}  // namespace vimbrowser
