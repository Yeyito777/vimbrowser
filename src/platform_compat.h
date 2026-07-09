#pragma once

// Portability shims for the Unix-socket IPC code shared across platforms.
//
// Darwin has no SOCK_CLOEXEC socket() flag and no accept4(). Defining the flag
// to 0 keeps the shared call sites compiling. The accept4() emulation always
// sets FD_CLOEXEC because SOCK_CLOEXEC necessarily arrives as 0 on Darwin.
// Listener/client fds created with socket() lose close-on-exec on Darwin,
// which is acceptable for the short-lived helper processes CEF spawns.
#if defined(__APPLE__)

#include <fcntl.h>
#include <sys/socket.h>

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif

static inline int accept4(int fd,
                          struct sockaddr* addr,
                          socklen_t* addrlen,
                          int flags) {
  (void)flags;
  const int client = accept(fd, addr, addrlen);
  if (client >= 0) {
    const int fd_flags = fcntl(client, F_GETFD);
    if (fd_flags >= 0) {
      fcntl(client, F_SETFD, fd_flags | FD_CLOEXEC);
    }
  }
  return client;
}

#endif  // defined(__APPLE__)
