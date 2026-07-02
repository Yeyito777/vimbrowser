#pragma once

// Portability shims for the Unix-socket IPC code shared across platforms.
//
// Darwin has no SOCK_CLOEXEC socket() flag and no accept4(). Defining the flag
// to 0 keeps the shared call sites compiling; accepted connections still get
// FD_CLOEXEC via the accept4() emulation below. Listener/client fds created
// with socket() lose close-on-exec on Darwin, which is acceptable for the
// short-lived helper processes CEF spawns.
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
  const int client = accept(fd, addr, addrlen);
  if (client >= 0 && flags != 0) {
    fcntl(client, F_SETFD, FD_CLOEXEC);
  }
  return client;
}

#endif  // defined(__APPLE__)
