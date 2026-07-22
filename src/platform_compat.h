#pragma once

// Platform adapters preserve close-on-exec semantics for the shared Unix IPC
// implementation. Darwin lacks SOCK_CLOEXEC and accept4(), so apply FD_CLOEXEC
// explicitly there instead of weakening behavior or redefining system APIs.

#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(__APPLE__)
inline int SetCloseOnExecOrClose(int fd) {
  if (fd < 0) {
    return fd;
  }
  const int flags = fcntl(fd, F_GETFD);
  if (flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0) {
    return fd;
  }
  const int error = errno;
  close(fd);
  errno = error;
  return -1;
}
#endif

inline int SocketCloseOnExec(int domain, int type, int protocol) {
#if defined(__APPLE__)
  return SetCloseOnExecOrClose(socket(domain, type, protocol));
#else
  return socket(domain, type | SOCK_CLOEXEC, protocol);
#endif
}

inline int AcceptCloseOnExec(int fd) {
#if defined(__APPLE__)
  return SetCloseOnExecOrClose(accept(fd, nullptr, nullptr));
#else
  return accept4(fd, nullptr, nullptr, SOCK_CLOEXEC);
#endif
}
