#include "ipc_server.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <condition_variable>

#include "browser_window.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"

namespace vimbrowser {
namespace {

struct PendingCommand {
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  std::string response;
};

class IpcCommandTask final : public CefTask {
 public:
  IpcCommandTask(BrowserWindow* owner,
                 std::string command,
                 std::shared_ptr<PendingCommand> pending)
      : owner_(owner), command_(std::move(command)), pending_(std::move(pending)) {}

  void Execute() override {
    std::string response;
    if (owner_) {
      response = owner_->HandleIpcCommand(command_);
    } else {
      response = "ERR no owner\n";
    }

    {
      std::lock_guard<std::mutex> lock(pending_->mutex);
      pending_->response = std::move(response);
      pending_->done = true;
    }
    pending_->cv.notify_one();
  }

 private:
  BrowserWindow* owner_ = nullptr;
  std::string command_;
  std::shared_ptr<PendingCommand> pending_;

  IMPLEMENT_REFCOUNTING(IpcCommandTask);
  DISALLOW_COPY_AND_ASSIGN(IpcCommandTask);
};

std::string TrimNewline(std::string value) {
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  return value;
}

}  // namespace

IpcServer::IpcServer(BrowserWindow* owner, std::string socket_path)
    : owner_(owner), socket_path_(std::move(socket_path)) {}

IpcServer::~IpcServer() {
  Stop();
}

bool IpcServer::Start() {
  if (running_) {
    return true;
  }

  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(socket_path_).parent_path(), ec);
  std::filesystem::remove(socket_path_, ec);

  server_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (server_fd_ < 0) {
    std::cerr << "vimbrowser: ipc socket() failed: " << std::strerror(errno) << std::endl;
    return false;
  }

  sockaddr_un addr = {};
  addr.sun_family = AF_UNIX;
  if (socket_path_.size() >= sizeof(addr.sun_path)) {
    std::cerr << "vimbrowser: ipc socket path too long: " << socket_path_ << std::endl;
    close(server_fd_);
    server_fd_ = -1;
    return false;
  }
  std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

  if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    std::cerr << "vimbrowser: ipc bind(" << socket_path_ << ") failed: "
              << std::strerror(errno) << std::endl;
    close(server_fd_);
    server_fd_ = -1;
    return false;
  }
  chmod(socket_path_.c_str(), 0600);

  if (listen(server_fd_, 16) != 0) {
    std::cerr << "vimbrowser: ipc listen() failed: " << std::strerror(errno) << std::endl;
    close(server_fd_);
    server_fd_ = -1;
    std::filesystem::remove(socket_path_, ec);
    return false;
  }

  running_ = true;
  thread_ = std::thread(&IpcServer::Loop, this);
  std::cout << "vimbrowser: ipc " << socket_path_ << std::endl;
  return true;
}

void IpcServer::Stop() {
  if (!running_.exchange(false)) {
    return;
  }

  if (server_fd_ >= 0) {
    shutdown(server_fd_, SHUT_RDWR);
    close(server_fd_);
    server_fd_ = -1;
  }

  if (thread_.joinable()) {
    thread_.join();
  }

  std::error_code ec;
  std::filesystem::remove(socket_path_, ec);
}

void IpcServer::Loop() {
  while (running_) {
    const int client_fd = accept4(server_fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (client_fd < 0) {
      if (running_) {
        std::cerr << "vimbrowser: ipc accept() failed: " << std::strerror(errno) << std::endl;
      }
      continue;
    }
    HandleClient(client_fd);
    close(client_fd);
  }
}

void IpcServer::HandleClient(int client_fd) {
  std::string command;
  char buffer[1024];
  while (command.size() < 8192) {
    const ssize_t n = read(client_fd, buffer, sizeof(buffer));
    if (n <= 0) {
      break;
    }
    command.append(buffer, static_cast<size_t>(n));
    if (command.find('\n') != std::string::npos) {
      break;
    }
  }
  command = TrimNewline(command);
  if (command.empty()) {
    return;
  }

  auto pending = std::make_shared<PendingCommand>();
  if (!CefPostTask(TID_UI, new IpcCommandTask(owner_, command, pending))) {
    const std::string error = "ERR failed to post ipc command\n";
    const ssize_t ignored = write(client_fd, error.data(), error.size());
    (void)ignored;
    return;
  }

  std::unique_lock<std::mutex> lock(pending->mutex);
  pending->cv.wait(lock, [&] { return pending->done; });
  if (pending->response.empty() || pending->response.back() != '\n') {
    pending->response.push_back('\n');
  }
  const ssize_t ignored =
      write(client_fd, pending->response.data(), pending->response.size());
  (void)ignored;
}

}  // namespace vimbrowser
