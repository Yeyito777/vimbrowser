#include "ipc_server.h"

#include <sys/socket.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "browser_window.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_closure_task.h"
#include "platform_compat.h"

namespace vimbrowser {
namespace {

struct PendingCommand {
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  bool dispatched = false;
  bool client_disconnected = false;
  bool timed_out = false;
  bool completed = false;
  std::string name;
  const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
  std::string response;
};

class IpcCommandTask final : public CefTask {
 public:
  IpcCommandTask(BrowserWindow* owner,
                 std::string command,
                 std::shared_ptr<PendingCommand> pending)
      : owner_(owner), command_(std::move(command)), pending_(std::move(pending)) {}

  void Execute() override {
    if (owner_) {
      {
        std::lock_guard<std::mutex> lock(pending_->mutex);
        if (pending_->done) {
          return;
        }
        pending_->dispatched = true;
      }
      owner_->HandleIpcCommandAsync(
          command_, [pending = pending_](std::string response) {
            {
              std::lock_guard<std::mutex> lock(pending->mutex);
              pending->completed = true;
              if (pending->done) {
                return;
              }
              pending->response = std::move(response);
              pending->done = true;
            }
            pending->cv.notify_one();
          });
      return;
    }

    {
      std::lock_guard<std::mutex> lock(pending_->mutex);
      pending_->response = "ERR no owner\n";
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

void WriteAll(int fd, const std::string& response) {
  const char* data = response.data();
  size_t remaining = response.size();
  while (remaining > 0) {
#if defined(MSG_NOSIGNAL)
    const ssize_t written = send(fd, data, remaining, MSG_NOSIGNAL);
#else
    const ssize_t written = write(fd, data, remaining);
#endif
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return;
    }
    if (written == 0) {
      return;
    }
    data += written;
    remaining -= static_cast<size_t>(written);
  }
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

  server_fd_ = SocketCloseOnExec(AF_UNIX, SOCK_STREAM, 0);
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
  std::memcpy(addr.sun_path, socket_path_.c_str(), socket_path_.size() + 1);

  const socklen_t addr_len = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + socket_path_.size() + 1);
  if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), addr_len) != 0) {
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

  // Independent transport liveness endpoint: still answers while the canonical
  // serial command queue is waiting for the UI or a renderer. No page access.
  const std::string health_path = socket_path_ + ".health";
  if (health_path.size() >= sizeof(addr.sun_path)) {
    close(server_fd_);
    server_fd_ = -1;
    std::filesystem::remove(socket_path_, ec);
    return false;
  }
  std::filesystem::remove(health_path, ec);
  health_fd_ = SocketCloseOnExec(AF_UNIX, SOCK_STREAM, 0);
  std::memset(addr.sun_path, 0, sizeof(addr.sun_path));
  std::memcpy(addr.sun_path, health_path.c_str(), health_path.size() + 1);
  if (health_fd_ < 0 ||
      bind(health_fd_, reinterpret_cast<sockaddr*>(&addr),
           offsetof(sockaddr_un, sun_path) + health_path.size() + 1) != 0 ||
      listen(health_fd_, 4) != 0) {
    if (health_fd_ >= 0) close(health_fd_);
    health_fd_ = -1;
    close(server_fd_);
    server_fd_ = -1;
    std::filesystem::remove(socket_path_, ec);
    std::filesystem::remove(health_path, ec);
    return false;
  }
  chmod(health_path.c_str(), 0600);
  running_ = true;
  health_thread_ = std::thread(&IpcServer::HealthLoop, this);
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

  if (health_fd_ >= 0) shutdown(health_fd_, SHUT_RDWR);
  if (health_thread_.joinable()) health_thread_.join();
  if (health_fd_ >= 0) close(health_fd_);
  health_fd_ = -1;

  const int client_fd = client_fd_.load();
  if (client_fd >= 0) {
    shutdown(client_fd, SHUT_RDWR);
  }

  std::function<void()> cancel_pending;
  {
    std::lock_guard<std::mutex> lock(pending_command_mutex_);
    cancel_pending = cancel_pending_command_;
  }
  if (cancel_pending) {
    cancel_pending();
  }

  if (thread_.joinable()) {
    thread_.join();
  }

  std::error_code ec;
  std::filesystem::remove(socket_path_, ec);
  std::filesystem::remove(socket_path_ + ".health", ec);
}

void IpcServer::HealthLoop() {
  while (running_) {
    const int fd = AcceptCloseOnExec(health_fd_);
    if (fd < 0) continue;
    timeval timeout = {0, 250000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    char ignored[64];
    const ssize_t read_result = read(fd, ignored, sizeof(ignored));
    (void)read_result;
    std::deque<std::function<std::string()>> snapshots;
    {
      std::lock_guard<std::mutex> lock(pending_command_mutex_);
      snapshots = operation_snapshots_;
    }
    std::string recent = "[";
    for (const auto& snapshot : snapshots) {
      if (recent.size() > 1) recent += ',';
      recent += snapshot();
    }
    recent += ']';
    WriteAll(fd, "{\"ok\":true,\"protocol\":1,\"pid\":" +
        std::to_string(getpid()) + ",\"shell_build\":\"" __DATE__ " " __TIME__
        "\",\"serial_queue\":true,\"operation\":" +
        (snapshots.empty() ? "null" : snapshots.back()()) +
        ",\"recent_operations\":" + recent + ",\"history_limit\":16}\n");
    close(fd);
  }
}

void IpcServer::Loop() {
  while (running_) {
    const int client_fd = AcceptCloseOnExec(server_fd_);
    if (client_fd < 0) {
      if (running_) {
        std::cerr << "vimbrowser: ipc accept() failed: " << std::strerror(errno) << std::endl;
      }
      continue;
    }
    client_fd_.store(client_fd);
    HandleClient(client_fd);
    client_fd_.store(-1);
    close(client_fd);
  }
}

void IpcServer::HandleClient(int client_fd) {
  // vimbrowser-ipc/1: one UTF-8-ish command line per connection, terminated by
  // '\n' or EOF; one '\n'-terminated response. This deliberately stays tiny and
  // scriptable, but it is the canonical app-control protocol. See docs/ipc.md.
  std::string command;
  command.reserve(128);
  char buffer[4096];
  timeval read_timeout = {2, 0};
  setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));
  setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &read_timeout, sizeof(read_timeout));
  constexpr size_t kMaxCommandBytes = 1024 * 1024;
  while (command.size() < kMaxCommandBytes) {
    const ssize_t n = read(client_fd, buffer, sizeof(buffer));
    if (n < 0) return;  // Never dispatch a partial command after a read timeout.
    if (n == 0) break;
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
  pending->name = command.substr(0, command.find(' '));
  if (pending->name.size() > 64 || pending->name.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos) {
    pending->name = "raw";
  }
  {
    std::lock_guard<std::mutex> lock(pending_command_mutex_);
    const uint64_t sequence = ++operation_sequence_;
    operation_snapshots_.push_back([pending, sequence]() {
      std::lock_guard<std::mutex> lock(pending->mutex);
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - pending->began).count();
      const char* state = pending->completed ? "completed" :
          pending->timed_out && pending->dispatched ? "timeout_outcome_unknown" :
          pending->done && pending->dispatched ? "abandoned_outcome_unknown" :
          pending->done ? "cancelled_before_dispatch" :
          pending->dispatched ? "in_flight" : "queued";
      return "{\"id\":" + std::to_string(sequence) + ",\"command\":\"" + pending->name + "\",\"state\":\"" + state +
          "\",\"age_ms\":" + std::to_string(elapsed) +
          ",\"client_disconnected\":" + (pending->client_disconnected ? "true" : "false") +
          ",\"deadline_exceeded\":" + (pending->timed_out ? "true" : "false") + "}";
    });
    if (operation_snapshots_.size() > 16) operation_snapshots_.pop_front();
  }
  auto cancel_pending = [pending]() {
    {
      std::lock_guard<std::mutex> lock(pending->mutex);
      if (pending->done) {
        return;
      }
      pending->response = "ERR ipc server shutting down\n";
      pending->done = true;
    }
    pending->cv.notify_one();
  };
  {
    std::lock_guard<std::mutex> lock(pending_command_mutex_);
    if (!running_) {
      cancel_pending();
    } else {
      cancel_pending_command_ = cancel_pending;
    }
  }
  pollfd peer = {client_fd, 0, 0};
  if (poll(&peer, 1, 0) > 0 && (peer.revents & (POLLHUP | POLLERR))) {
    std::lock_guard<std::mutex> lock(pending->mutex);
    pending->client_disconnected = true;
    pending->done = true;
  }
  if (!CefPostTask(TID_UI, new IpcCommandTask(owner_, command, pending))) {
    {
      std::lock_guard<std::mutex> lock(pending->mutex);
      pending->done = true;
    }
    {
      std::lock_guard<std::mutex> lock(pending_command_mutex_);
      cancel_pending_command_ = nullptr;
    }
    const std::string error = "ERR failed to post ipc command\n";
    const ssize_t ignored = write(client_fd, error.data(), error.size());
    (void)ignored;
    return;
  }

  std::unique_lock<std::mutex> lock(pending->mutex);
  const auto deadline = pending->began + std::chrono::seconds(35);
  while (!pending->done && std::chrono::steady_clock::now() < deadline) {
    pending->cv.wait_for(lock, std::chrono::milliseconds(50));
    pollfd peer = {client_fd, 0, 0};
    if (poll(&peer, 1, 0) > 0 && (peer.revents & (POLLHUP | POLLERR))) {
      pending->client_disconnected = true;
      // Only a not-yet-dispatched command can safely be cancelled. In-flight
      // side effects cannot be rolled back by closing the client's socket.
      if (!pending->dispatched) pending->done = true;
    }
  }
  if (!pending->done) {
    pending->response = "ERR ipc deadline exceeded; dispatched operation outcome unknown; do not blindly retry\n";
    pending->timed_out = true;
    pending->done = true;
  }
  {
    std::lock_guard<std::mutex> pending_lock(pending_command_mutex_);
    cancel_pending_command_ = nullptr;
  }
  if (pending->response.empty() || pending->response.back() != '\n') {
    pending->response.push_back('\n');
  }
  // Diagnostic history retains metadata, never page/command response content.
  // A slow reader must not hold the metadata lock and block the health thread.
  std::string response = std::move(pending->response);
  lock.unlock();
  WriteAll(client_fd, response);
}

}  // namespace vimbrowser
