// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains functions for launching subprocesses.

#ifndef BASE_PROCESS_LAUNCH_H_
#define BASE_PROCESS_LAUNCH_H_

#include <limits.h>
#include <stddef.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base_export.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/functional/function_ref.h"
#include "base/memory/raw_ptr.h"
#include "base/process/process.h"
#include "base/process/process_handle.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "build/blink_buildflags.h"
#include "build/build_config.h"


#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
#include "base/posix/file_descriptor_shuffle.h"
#endif

#if BUILDFLAG(IS_MAC)
#include "base/mac/process_requirement.h"
#endif

namespace base {

enum TerminationStatus : int;

// Some code (e.g. the sandbox) relies on PTHREAD_STACK_MIN
// being async-signal-safe, which is no longer guaranteed by POSIX
// (it may call sysconf, which is not safe).
// To work around this, use a hardcoded value unless it's already
// defined as a constant.

// These constants are borrowed from glibc's (arch)/bits/pthread_stack_min.h.
#if defined(ARCH_CPU_ARM64) || defined(ARCH_CPU_LOONGARCH64)
#define PTHREAD_STACK_MIN_CONST \
  (__builtin_constant_p(PTHREAD_STACK_MIN) ? PTHREAD_STACK_MIN : 131072)
#else
#define PTHREAD_STACK_MIN_CONST \
  (__builtin_constant_p(PTHREAD_STACK_MIN) ? PTHREAD_STACK_MIN : 16384)
#endif  // defined(ARCH_CPU_ARM64)

static_assert(__builtin_constant_p(PTHREAD_STACK_MIN_CONST),
              "must be constant");

// Make sure our hardcoded value is large enough to accommodate the
// actual minimum stack size. This function will run a one-time CHECK
// and so should be called at some point, preferably during startup.
BASE_EXPORT void CheckPThreadStackMinIsSafe();

#if BUILDFLAG(IS_APPLE) && !BUILDFLAG(IS_IOS_TVOS)
class MachRendezvousPort;
using MachPortsForRendezvous = std::map<uint32_t, MachRendezvousPort>;
#endif

typedef std::vector<std::pair<int, int>> FileHandleMappingVector;

// Options for launching a subprocess that are passed to LaunchProcess().
// The default constructor constructs the object with default options.
struct BASE_EXPORT LaunchOptions {
#if (BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)) && !BUILDFLAG(IS_APPLE)
  // Delegate to be run in between fork and exec in the subprocess (see
  // pre_exec_delegate below)
  class BASE_EXPORT PreExecDelegate {
   public:
    PreExecDelegate() = default;

    PreExecDelegate(const PreExecDelegate&) = delete;
    PreExecDelegate& operator=(const PreExecDelegate&) = delete;

    virtual ~PreExecDelegate() = default;

    // Since this is to be run between fork and exec, and fork may have happened
    // while multiple threads were running, this function needs to be async
    // safe.
    virtual void RunAsyncSafe() = 0;
  };
#endif  // BUILDFLAG(IS_POSIX)

  LaunchOptions();
  LaunchOptions(const LaunchOptions&);
  ~LaunchOptions();

  // If true, wait for the process to complete.
  bool wait = false;

  // If not empty, change to this directory before executing the new process.
  base::FilePath current_directory;

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  // Remap file descriptors according to the mapping of src_fd->dest_fd to
  // propagate FDs into the child process.
  FileHandleMappingVector fds_to_remap;
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  // Set/unset environment variables. These are applied on top of the parent
  // process environment.  Empty (the default) means to inherit the same
  // environment. See internal::AlterEnvironment().
  EnvironmentMap environment;

  // Clear the environment for the new process before processing changes from
  // |environment|.
  bool clear_environment = false;
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  // If non-zero, start the process using clone(), using flags as provided.
  // Unlike in clone, clone_flags may not contain a custom termination signal
  // that is sent to the parent when the child dies. The termination signal will
  // always be set to SIGCHLD.
  int clone_flags = 0;

  // By default, child processes will have the PR_SET_NO_NEW_PRIVS bit set. If
  // true, then this bit will not be set in the new child process.
  bool allow_new_privs = false;

  // Sets parent process death signal to SIGKILL.
  bool kill_on_parent_death = false;

  // File descriptors of the parent process with FD_CLOEXEC flag to be removed
  // before calling exec*().
  std::vector<int> fds_to_remove_cloexec;
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_MAC) || (BUILDFLAG(IS_IOS) && BUILDFLAG(USE_BLINK))
  // Mach ports that will be accessible to the child process. These are not
  // directly inherited across process creation, but they are stored by a Mach
  // IPC server that a child process can communicate with to retrieve them.
  //
  // After calling LaunchProcess(), any rights that were transferred with MOVE
  // dispositions will be consumed, even on failure.
  //
  // See base/apple/mach_port_rendezvous.h for details.
  MachPortsForRendezvous mach_ports_for_rendezvous;

  // Apply a process scheduler policy to enable mitigations against CPU side-
  // channel attacks.
  bool enable_cpu_security_mitigations = false;
#endif  // BUILDFLAG(IS_MAC) || (BUILDFLAG(IS_IOS) && BUILDFLAG(USE_BLINK))

#if BUILDFLAG(IS_MAC)
  // When a child process is launched, the system tracks the parent process
  // with a concept of "responsibility". The responsible process will be
  // associated with any requests for private data stored on the system via
  // the TCC subsystem. When launching processes that run foreign/third-party
  // code, the responsibility for the child process should be disclaimed so
  // that any TCC requests are not associated with the parent.
  bool disclaim_responsibility = false;

  // A `ProcessRequirement` that will be used to validate the launched process
  // before it can retrieve `mach_ports_for_rendezvous`.
  std::optional<mac::ProcessRequirement> process_requirement;
#endif  // BUILDFLAG(IS_MAC)


  // If not empty, launch the specified executable instead of
  // cmdline.GetProgram(). This is useful when it is necessary to pass a custom
  // argv[0].
  base::FilePath real_path;

#if !BUILDFLAG(IS_APPLE)
  // If non-null, a delegate to be run immediately prior to executing the new
  // program in the child process.
  //
  // WARNING: If LaunchProcess is called in the presence of multiple threads,
  // code running in this delegate essentially needs to be async-signal safe
  // (see man 7 signal for a list of allowed functions).
  raw_ptr<PreExecDelegate> pre_exec_delegate = nullptr;
#endif  // !BUILDFLAG(IS_APPLE)

  // Each element is an RLIMIT_* constant that should be raised to its
  // rlim_max.  This pointer is owned by the caller and must live through
  // the call to LaunchProcess().
  raw_ptr<const std::vector<int>> maximize_rlimits = nullptr;

  // If true, start the process in a new process group, instead of
  // inheriting the parent's process group.  The pgid of the child process
  // will be the same as its pid.
  bool new_process_group = false;

};

// Launch a process via the command line |cmdline|.
// See the documentation of LaunchOptions for details on |options|.
//
// Returns a valid Process upon success.
//
// Unix-specific notes:
// - All file descriptors open in the parent process will be closed in the
//   child process except for any preserved by options::fds_to_remap, and
//   stdin, stdout, and stderr. If not remapped by options::fds_to_remap,
//   stdin is reopened as /dev/null, and the child is allowed to inherit its
//   parent's stdout and stderr.
// - If the first argument on the command line does not contain a slash,
//   PATH will be searched.  (See man execvp.)
BASE_EXPORT Process LaunchProcess(const CommandLine& cmdline,
                                  const LaunchOptions& options);

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
// A POSIX-specific version of LaunchProcess that takes an argv array
// instead of a CommandLine.  Useful for situations where you need to
// control the command line arguments directly, but prefer the
// CommandLine version if launching Chrome itself.
BASE_EXPORT Process LaunchProcess(const std::vector<std::string>& argv,
                                  const LaunchOptions& options);

#if !BUILDFLAG(IS_APPLE)
// Close all file descriptors, except those which are a destination in the
// given multimap. Only call this function in a child process where you know
// that there aren't any other threads.
BASE_EXPORT void CloseSuperfluousFds(const InjectiveMultimap& saved_map);
#endif  // BUILDFLAG(IS_APPLE)
#endif  // BUILDFLAG(IS_WIN)


// Executes the application specified by |cl| and wait for it to exit. Stores
// the output (stdout) in |output|. Redirects stderr to /dev/null. Returns true
// on success (application launched and exited cleanly, with exit code
// indicating success).
BASE_EXPORT bool GetAppOutput(const CommandLine& cl, std::string* output);

// Like GetAppOutput, but also includes stderr.
BASE_EXPORT bool GetAppOutputAndError(const CommandLine& cl,
                                      std::string* output);

// A version of |GetAppOutput()| which also returns the exit code of the
// executed command. Returns true if the application runs and exits cleanly. If
// this is the case the exit code of the application is available in
// |*exit_code|.
BASE_EXPORT bool GetAppOutputWithExitCode(const CommandLine& cl,
                                          std::string* output,
                                          int* exit_code);

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
// A POSIX-specific version of GetAppOutput that takes an argv array
// instead of a CommandLine.  Useful for situations where you need to
// control the command line arguments directly.
BASE_EXPORT bool GetAppOutput(const std::vector<std::string>& argv,
                              std::string* output);

// Like the above POSIX-specific version of GetAppOutput, but also includes
// stderr.
BASE_EXPORT bool GetAppOutputAndError(const std::vector<std::string>& argv,
                                      std::string* output);

// Like the POSIX-specific GetAppOutput above, but also includes the exit code.
BASE_EXPORT bool GetAppOutputWithExitCode(const std::vector<std::string>& argv,
                                          std::string* output,
                                          int* exit_code);
#endif  // BUILDFLAG(IS_WIN)

// If supported on the platform, and the user has sufficent rights, increase
// the current process's scheduling priority to a high priority.
BASE_EXPORT void RaiseProcessToHighPriority();

// Creates a LaunchOptions object suitable for launching processes in a test
// binary. This should not be called in production/released code.
BASE_EXPORT LaunchOptions LaunchOptionsForTest();

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
// A wrapper for clone with fork-like behavior, meaning that it returns the
// child's pid in the parent and 0 in the child. |flags|, |ptid|, and |ctid| are
// as in the clone system call (the CLONE_VM flag is not supported).
//
// This function uses the libc clone wrapper (which updates libc's pid cache)
// internally, so callers may expect things like getpid() to work correctly
// after in both the child and parent.
//
// As with fork(), callers should be extremely careful when calling this while
// multiple threads are running, since at the time the fork happened, the
// threads could have been in any state (potentially holding locks, etc.).
// Callers should most likely call execve() in the child soon after calling
// this.
//
// It is unsafe to use any pthread APIs after ForkWithFlags().
// However, performing an exec() will lift this restriction.
BASE_EXPORT pid_t ForkWithFlags(int flags, pid_t* ptid, pid_t* ctid);
#endif

namespace internal {

// Friend and derived class of ScopedAllowBaseSyncPrimitives which allows
// GetAppOutputInternal() to join a process. GetAppOutputInternal() can't itself
// be a friend of ScopedAllowBaseSyncPrimitives because it is in the anonymous
// namespace.
class [[maybe_unused, nodiscard]] GetAppOutputScopedAllowBaseSyncPrimitives
    : public base::ScopedAllowBaseSyncPrimitives {};

}  // namespace internal

}  // namespace base

#endif  // BASE_PROCESS_LAUNCH_H_
