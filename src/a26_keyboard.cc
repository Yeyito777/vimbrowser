#include "a26_keyboard.h"

#if defined(__linux__)
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace vimbrowser {
namespace {

constexpr int kRetryDelayMs = 100;
constexpr int kShowAttempts = 25;
constexpr int kHideAttempts = 3;

#if defined(__linux__)
constexpr char kA26ControlSocketPath[] = "/run/a26-shell/control.sock";
constexpr int kSocketTotalTimeoutMs = 120;
constexpr size_t kMaxResponseBytes = 4096;

class ScopedSocket final {
 public:
  explicit ScopedSocket(int fd) : fd_(fd) {}
  ~ScopedSocket() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

 private:
  int fd_;
};

const char* CommandForPurpose(A26KeyboardPurpose purpose) {
  switch (purpose) {
    case A26KeyboardPurpose::kHide:
      return "keyboard hide\n";
    case A26KeyboardPurpose::kText:
      return "keyboard show text\n";
    case A26KeyboardPurpose::kPassword:
      return "keyboard show password\n";
    case A26KeyboardPurpose::kSearch:
      return "keyboard show search\n";
    case A26KeyboardPurpose::kUrl:
      return "keyboard show url\n";
    case A26KeyboardPurpose::kNumber:
      return "keyboard show number\n";
  }
  return "keyboard hide\n";
}

using SocketDeadline = std::chrono::steady_clock::time_point;

bool WaitForSocket(int fd, short events, SocketDeadline deadline) {
  for (;;) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) {
      return false;
    }
    pollfd descriptor = {fd, events, 0};
    const int result = poll(&descriptor, 1, static_cast<int>(remaining.count()));
    if (result < 0 && errno == EINTR) {
      continue;
    }
    return result == 1 && !(descriptor.revents & (POLLERR | POLLNVAL)) &&
           (descriptor.revents & events);
  }
}

const char* PurposeName(A26KeyboardPurpose purpose) {
  switch (purpose) {
    case A26KeyboardPurpose::kText:
      return "text";
    case A26KeyboardPurpose::kPassword:
      return "password";
    case A26KeyboardPurpose::kSearch:
      return "search";
    case A26KeyboardPurpose::kUrl:
      return "url";
    case A26KeyboardPurpose::kNumber:
      return "number";
    case A26KeyboardPurpose::kHide:
      return nullptr;
  }
  return nullptr;
}

bool ResponseApplied(A26KeyboardPurpose purpose, const std::string& response) {
  if (response.find("\"ok\":true") == std::string::npos) {
    return false;
  }
  if (purpose == A26KeyboardPurpose::kHide) {
    return response.find("\"visible\":false") != std::string::npos;
  }
  const char* name = PurposeName(purpose);
  return name && response.find("\"visible\":true") != std::string::npos &&
         response.find(std::string("\"purpose\":\"") + name + "\"") !=
             std::string::npos;
}

bool SendControlCommand(A26KeyboardPurpose purpose) {
  // SOCK_NONBLOCK bounds every operation and SOCK_CLOEXEC guarantees that the
  // shell socket can never leak into Chromium subprocesses.
  const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    return false;
  }
  ScopedSocket socket(fd);
  const SocketDeadline deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(
                                      kSocketTotalTimeoutMs);

  sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  static_assert(sizeof(kA26ControlSocketPath) <= sizeof(address.sun_path));
  std::memcpy(address.sun_path, kA26ControlSocketPath,
              sizeof(kA26ControlSocketPath));

  const int connect_result =
      connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
  if (connect_result < 0 && errno != EINPROGRESS) {
    return false;
  }
  if (connect_result < 0) {
    if (!WaitForSocket(fd, POLLOUT, deadline)) {
      return false;
    }
    int socket_error = 0;
    socklen_t socket_error_size = sizeof(socket_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                   &socket_error_size) != 0 ||
        socket_error != 0) {
      return false;
    }
  }

  const char* command = CommandForPurpose(purpose);
  const size_t command_size = std::strlen(command);
  size_t sent = 0;
  while (sent < command_size) {
    const ssize_t result =
        send(fd, command + sent, command_size - sent, MSG_NOSIGNAL);
    if (result > 0) {
      sent += static_cast<size_t>(result);
      continue;
    }
    if (result < 0 && errno == EINTR &&
        std::chrono::steady_clock::now() < deadline) {
      continue;
    }
    if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
        WaitForSocket(fd, POLLOUT, deadline)) {
      continue;
    }
    return false;
  }

  // Read one complete bounded response line so launch-time rejections can be
  // retried. The response is never logged or exposed to page code.
  std::string response;
  response.reserve(512);
  while (response.size() < kMaxResponseBytes) {
    if (!WaitForSocket(fd, POLLIN, deadline)) {
      return false;
    }
    char chunk[256];
    const size_t remaining = kMaxResponseBytes - response.size();
    ssize_t received;
    do {
      received = recv(fd, chunk, std::min(sizeof(chunk), remaining), 0);
    } while (received < 0 && errno == EINTR &&
             std::chrono::steady_clock::now() < deadline);
    if (received <= 0) {
      return false;
    }
    response.append(chunk, static_cast<size_t>(received));
    const size_t newline = response.find('\n');
    if (newline != std::string::npos) {
      response.resize(newline);
      return ResponseApplied(purpose, response);
    }
  }
  return false;
}
#else
bool SendControlCommand(A26KeyboardPurpose) {
  return false;
}
#endif

}  // namespace

A26KeyboardClient::A26KeyboardClient()
    : worker_(&A26KeyboardClient::Run, this) {}

A26KeyboardClient::~A26KeyboardClient() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  condition_.notify_one();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void A26KeyboardClient::Request(A26KeyboardPurpose purpose) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || pending_ == purpose) {
      return;
    }
    pending_ = purpose;
  }
  condition_.notify_one();
}

void A26KeyboardClient::Run() {
  for (;;) {
    A26KeyboardPurpose purpose;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      condition_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
      if (!pending_) {
        return;
      }
      purpose = *pending_;
      pending_.reset();
    }

    const int attempts = purpose == A26KeyboardPurpose::kHide ? kHideAttempts
                                                              : kShowAttempts;
    for (int attempt = 0; attempt < attempts; ++attempt) {
      if (SendControlCommand(purpose)) {
        break;
      }
      std::unique_lock<std::mutex> lock(mutex_);
      if (condition_.wait_for(lock, std::chrono::milliseconds(kRetryDelayMs),
                              [this] { return stopping_ || pending_.has_value(); })) {
        break;
      }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ && !pending_) {
      return;
    }
  }
}

}  // namespace vimbrowser
