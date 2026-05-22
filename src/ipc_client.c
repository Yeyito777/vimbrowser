#include <errno.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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

static char* build_command_line(int argc, char** argv, size_t* out_length) {
  if (argc <= 1) {
    char* command = malloc(7);
    if (!command) {
      return NULL;
    }
    memcpy(command, "status\n", 7);
    *out_length = 7;
    return command;
  }

  size_t length = 1;  // trailing newline
  for (int i = 1; i < argc; ++i) {
    const size_t arg_length = strlen(argv[i]);
    if (arg_length > SIZE_MAX - length - (i > 1 ? 1 : 0)) {
      return NULL;
    }
    length += arg_length + (i > 1 ? 1 : 0);
  }

  char* command = malloc(length);
  if (!command) {
    return NULL;
  }
  char* out = command;
  for (int i = 1; i < argc; ++i) {
    if (i > 1) {
      *out++ = ' ';
    }
    const size_t arg_length = strlen(argv[i]);
    memcpy(out, argv[i], arg_length);
    out += arg_length;
  }
  *out++ = '\n';
  *out_length = length;
  return command;
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
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  memcpy(addr.sun_path, path, strlen(path) + 1);
  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    fprintf(stderr, "vimbrowser-ipc: connect %s: %s\n", path, strerror(errno));
    close(fd);
    return 1;
  }

  size_t command_length = 0;
  char* command = build_command_line(argc, argv, &command_length);
  if (!command) {
    fprintf(stderr, "vimbrowser-ipc: command too large or out of memory\n");
    close(fd);
    return 1;
  }
  if (write_all(fd, command, command_length) < 0) {
    fprintf(stderr, "vimbrowser-ipc: write: %s\n", strerror(errno));
    free(command);
    close(fd);
    return 1;
  }
  free(command);

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
