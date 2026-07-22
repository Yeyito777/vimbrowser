#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef VIMBROWSER_SOURCE_DIR
#define VIMBROWSER_SOURCE_DIR "."
#endif

static bool executable_exists(const char* path) {
  return path && access(path, X_OK) == 0;
}

static void exec_screenshot_helper(int argc, char** argv) {
  char self_dir_buf[4096];
  char helper_path[4096];
  if (argv[0] && strlen(argv[0]) < sizeof(self_dir_buf)) {
    strcpy(self_dir_buf, argv[0]);
    char* dir = dirname(self_dir_buf);
    if (dir) {
      snprintf(helper_path, sizeof(helper_path), "%s/vimbrowser-ipc-screenshot", dir);
      if (executable_exists(helper_path)) {
        execv(helper_path, argv);
        fprintf(stderr, "vimbrowser-ipc: exec %s: %s\n", helper_path, strerror(errno));
        exit(1);
      }
    }
  }

  snprintf(helper_path, sizeof(helper_path), "%s/scripts/vimbrowser-ipc-screenshot", VIMBROWSER_SOURCE_DIR);
  if (executable_exists(helper_path)) {
    execv(helper_path, argv);
    fprintf(stderr, "vimbrowser-ipc: exec %s: %s\n", helper_path, strerror(errno));
    exit(1);
  }

  execvp("vimbrowser-ipc-screenshot", argv);
  fprintf(stderr, "vimbrowser-ipc: exec vimbrowser-ipc-screenshot: %s\n", strerror(errno));
  exit(1);
}

static int write_all(int fd, const char* buffer, size_t length) {
  while (length > 0) {
    ssize_t written = write(fd, buffer, length);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    buffer += written;
    length -= (size_t)written;
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc > 1 && strcmp(argv[1], "screenshot") == 0) {
    exec_screenshot_helper(argc, argv);
  }

  char path_buffer[4096];
  const char* path = getenv("VIMBROWSER_IPC");
  if (!(path && *path)) {
    const char* profile = getenv("VIMBROWSER_PROFILE_DIR");
    if (profile && *profile) {
      snprintf(path_buffer, sizeof(path_buffer), "%s/ipc.sock", profile);
      path = path_buffer;
    } else if (access("/home/yeyito/.runtime/vimbrowser-yeyito/ipc.sock", F_OK) == 0) {
      path = "/home/yeyito/.runtime/vimbrowser-yeyito/ipc.sock";
    } else {
      const char* state_home = getenv("XDG_STATE_HOME");
      if (state_home && *state_home) {
        snprintf(path_buffer, sizeof(path_buffer), "%s/vimbrowser/ipc.sock", state_home);
        path = path_buffer;
      } else {
        const char* home = getenv("HOME");
        if (home && *home) {
          snprintf(path_buffer, sizeof(path_buffer), "%s/.local/state/vimbrowser/ipc.sock", home);
          path = path_buffer;
        } else {
          path = "/tmp/vimbrowser/ipc.sock";
        }
      }
    }
  }

  struct sockaddr_un addr;
  if (strlen(path) >= sizeof(addr.sun_path)) {
    fprintf(stderr, "vimbrowser-ipc: socket path too long: %s\n", path);
    return 1;
  }

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    fprintf(stderr, "vimbrowser-ipc: socket: %s\n", strerror(errno));
    return 1;
  }
  const int fd_flags = fcntl(fd, F_GETFD);
  if (fd_flags >= 0) {
    fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC);
  }
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  memcpy(addr.sun_path, path, strlen(path) + 1);
  const socklen_t addr_len =
      (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1);
  if (connect(fd, (struct sockaddr*)&addr, addr_len) < 0) {
    fprintf(stderr, "vimbrowser-ipc: connect %s: %s\n", path, strerror(errno));
    close(fd);
    return 1;
  }

  if (argc <= 1) {
    if (write_all(fd, "status\n", 7) < 0) {
      fprintf(stderr, "vimbrowser-ipc: write: %s\n", strerror(errno));
      close(fd);
      return 1;
    }
  } else {
    for (int i = 1; i < argc; ++i) {
      if (i > 1 && write_all(fd, " ", 1) < 0) {
        fprintf(stderr, "vimbrowser-ipc: write: %s\n", strerror(errno));
        close(fd);
        return 1;
      }
      if (write_all(fd, argv[i], strlen(argv[i])) < 0) {
        fprintf(stderr, "vimbrowser-ipc: write: %s\n", strerror(errno));
        close(fd);
        return 1;
      }
    }
    if (write_all(fd, "\n", 1) < 0) {
      fprintf(stderr, "vimbrowser-ipc: write: %s\n", strerror(errno));
      close(fd);
      return 1;
    }
  }

  char buffer[262144];
  for (;;) {
    ssize_t read_bytes = read(fd, buffer, sizeof(buffer));
    if (read_bytes < 0) {
      if (errno == EINTR) {
        continue;
      }
      fprintf(stderr, "vimbrowser-ipc: recv: %s\n", strerror(errno));
      close(fd);
      return 1;
    }
    if (read_bytes == 0) {
      break;
    }
    if (write_all(STDOUT_FILENO, buffer, (size_t)read_bytes) < 0) {
      fprintf(stderr, "vimbrowser-ipc: write: %s\n", strerror(errno));
      close(fd);
      return 1;
    }
  }

  close(fd);
  return 0;
}
