#pragma once

#include <atomic>
#include <string>
#include <thread>

namespace vimbrowser {

class BrowserWindow;

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
