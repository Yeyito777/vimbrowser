// Copyright 2014 The Crashpad Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "handler/handler_main.h"

#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "base/files/file_path.h"
#include "base/files/scoped_file.h"
#include "base/logging.h"
#include "base/logging/logging_settings.h"
#include "base/metrics/persistent_histogram_allocator.h"
#include "base/scoped_generic.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "cef/libcef/features/features.h"
#include "client/crash_report_database.h"
#include "client/crashpad_client.h"
#include "client/crashpad_info.h"
#include "client/prune_crash_reports.h"
#include "client/simple_string_dictionary.h"
#include "handler/crash_report_upload_thread.h"
#include "handler/prune_crash_reports_thread.h"
#include "tools/tool_support.h"
#include "util/file/file_io.h"
#include "util/misc/address_types.h"
#include "util/misc/metrics.h"
#include "util/misc/paths.h"
#include "util/numeric/in_range_cast.h"
#include "util/stdlib/map_insert.h"
#include "util/stdlib/string_number_conversion.h"
#include "util/string/split_string.h"
#include "util/synchronization/semaphore.h"


#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
#include <unistd.h>

#include "handler/linux/crash_report_exception_handler.h"
#include "handler/linux/exception_handler_server.h"
#include "util/posix/signals.h"
#elif BUILDFLAG(IS_APPLE)
#include <libgen.h>
#include <signal.h>

#include "base/apple/scoped_mach_port.h"
#include "handler/mac/crash_report_exception_handler.h"
#include "handler/mac/exception_handler_server.h"
#include "handler/mac/file_limit_annotation.h"
#include "util/mach/bootstrap.h"
#include "util/mach/child_port_handshake.h"
#include "util/posix/close_stdio.h"
#include "util/posix/signals.h"
#endif  // BUILDFLAG(IS_APPLE)

#if BUILDFLAG(ENABLE_CEF)
#include "cef/libcef/common/cef_crash_report_upload_thread.h"
#endif

namespace crashpad {

namespace {

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \
    BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_MAC)
#define ATTACHMENTS_SUPPORTED 1
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) ||
        // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_MAC)

void Usage(const base::FilePath& me) {
  // clang-format off
  fprintf(stderr,
"Usage: %" PRFilePath " [OPTION]...\n"
"Crashpad's exception handler server.\n"
"\n"
"      --annotation=KEY=VALUE  set a process annotation in each crash report\n"
  // clang-format on
#if defined(ATTACHMENTS_SUPPORTED)
      // clang-format off
"      --attachment=FILE_PATH  attach specified file to each crash report\n"
"                              at the time of the crash\n"
  // clang-format on
#endif  // ATTACHMENTS_SUPPORTED
      // clang-format off
"      --database=PATH         store the crash report database at PATH\n"
  // clang-format on
#if BUILDFLAG(IS_APPLE)
      // clang-format off
"      --handshake-fd=FD       establish communication with the client over FD\n"
  // clang-format on
#endif  // BUILDFLAG(IS_APPLE)
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
      // clang-format off
"      --initial-client-fd=FD  a socket connected to a client.\n"
  // clang-format on
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS)
#if BUILDFLAG(IS_APPLE)
      // clang-format off
"      --mach-service=SERVICE  register SERVICE with the bootstrap server\n"
  // clang-format on
#endif  // BUILDFLAG(IS_APPLE)
      // clang-format off
"      --metrics-dir=DIR       store metrics files in DIR (only in Chromium)\n"
"      --monitor-self          run a second handler to catch crashes in the first\n"
"      --monitor-self-annotation=KEY=VALUE\n"
"                              set a module annotation in the handler\n"
"      --monitor-self-argument=ARGUMENT\n"
"                              provide additional arguments to the second handler\n"
"      --no-identify-client-via-url\n"
"                              when uploading crash report, don't add\n"
"                              client-identifying arguments to URL\n"
"      --no-periodic-tasks     don't scan for new reports or prune the database\n"
"      --no-rate-limit         don't rate limit crash uploads\n"
"      --no-upload-gzip        don't use gzip compression when uploading\n"
  // clang-format on
#if BUILDFLAG(IS_APPLE)
      // clang-format off
"      --reset-own-crash-exception-port-to-system-default\n"
"                              reset the server's exception handler to default\n"
  // clang-format on
#endif  // BUILDFLAG(IS_APPLE)
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
      // clang-format off
"      --sanitization-information=SANITIZATION_INFORMATION_ADDRESS\n"
"                              the address of a SanitizationInformation struct.\n"
"      --shared-client-connection the file descriptor provided by\n"
"                              --initial-client-fd is shared among multiple\n"
"                              clients\n"
"      --trace-parent-with-exception=EXCEPTION_INFORMATION_ADDRESS\n"
"                              request a dump for the handler's parent process\n"
  // clang-format on
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) ||
        // BUILDFLAG(IS_ANDROID)
      // clang-format off
"      --url=URL               send crash reports to this Breakpad server URL,\n"
"                              only if uploads are enabled for the database\n"
  // clang-format on
      // clang-format off
"      --help                  display this help and exit\n"
"      --version               output version information and exit\n",
          me.value().c_str());
  // clang-format on
  ToolSupport::UsageTail(me);
}

struct Options {
  std::map<std::string, std::string> annotations;
  std::map<std::string, std::string> monitor_self_annotations;
  std::string url;
  base::FilePath database;
  base::FilePath metrics_dir;
  std::vector<std::string> monitor_self_arguments;
#if BUILDFLAG(IS_APPLE)
  std::string mach_service;
  int handshake_fd;
  bool reset_own_crash_exception_port_to_system_default;
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
  VMAddress exception_information_address;
  VMAddress sanitization_information_address;
  int initial_client_fd;
  bool shared_client_connection;
#endif  // BUILDFLAG(IS_APPLE)
  bool identify_client_via_url;
  bool monitor_self;
  bool periodic_tasks;
  bool rate_limit;
  bool upload_gzip;
  int max_uploads;
  int max_database_size;
  int max_database_age;
#if defined(ATTACHMENTS_SUPPORTED)
  std::vector<base::FilePath> attachments;
#endif  // ATTACHMENTS_SUPPORTED
};

// Splits |key_value| on '=' and inserts the resulting key and value into |map|.
// If |key_value| has the wrong format, logs an error and returns false. If the
// key is already in the map, logs a warning, replaces the existing value, and
// returns true. If the key and value were inserted into the map, returns true.
// |argument| is used to give context to logged messages.
bool AddKeyValueToMap(std::map<std::string, std::string>* map,
                      const std::string& key_value,
                      const char* argument) {
  std::string key;
  std::string value;
  if (!SplitStringFirst(key_value, '=', &key, &value)) {
    LOG(ERROR) << argument << " requires KEY=VALUE";
    return false;
  }

  std::string old_value;
  if (!MapInsertOrReplace(map, key, value, &old_value)) {
    LOG(WARNING) << argument << " has duplicate key " << key
                 << ", discarding value " << old_value;
  }
  return true;
}

// Calls Metrics::HandlerLifetimeMilestone, but only on the first call. This is
// to prevent multiple exit events from inadvertently being recorded, which
// might happen if a crash occurs during destruction in what would otherwise be
// a normal exit, or if a CallMetricsRecordNormalExit object is destroyed after
// something else logs an exit event.
void MetricsRecordExit(Metrics::LifetimeMilestone milestone) {
#if !defined(__cpp_lib_atomic_value_initialization) || \
    __cpp_lib_atomic_value_initialization < 201911L
  static std::atomic_flag metrics_exit_recorded = ATOMIC_FLAG_INIT;
#else
  static std::atomic_flag metrics_exit_recorded;
#endif
  if (!metrics_exit_recorded.test_and_set()) {
    Metrics::HandlerLifetimeMilestone(milestone);
  }
}

// Calls MetricsRecordExit() to record a failure, and returns EXIT_FAILURE for
// the convenience of callers in main() which can simply write “return
// ExitFailure();”.
int ExitFailure() {
  MetricsRecordExit(Metrics::LifetimeMilestone::kFailed);
  return EXIT_FAILURE;
}

class CallMetricsRecordNormalExit {
 public:
  CallMetricsRecordNormalExit() {}

  CallMetricsRecordNormalExit(const CallMetricsRecordNormalExit&) = delete;
  CallMetricsRecordNormalExit& operator=(const CallMetricsRecordNormalExit&) =
      delete;

  ~CallMetricsRecordNormalExit() {
    MetricsRecordExit(Metrics::LifetimeMilestone::kExitedNormally);
  }
};

#if BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \
    BUILDFLAG(IS_ANDROID)

void HandleCrashSignal(int sig, siginfo_t* siginfo, void* context) {
  MetricsRecordExit(Metrics::LifetimeMilestone::kCrashed);

  // Is siginfo->si_code useful? The only interesting values on macOS are 0 (not
  // useful, signals generated asynchronously such as by kill() or raise()) and
  // small positive numbers (useful, signal generated via a hardware fault). The
  // standard specifies these other constants, and while xnu never uses them,
  // they are intended to denote signals generated asynchronously and are
  // included here. Additionally, existing practice on other systems
  // (acknowledged by the standard) is for negative numbers to indicate that a
  // signal was generated asynchronously. Although xnu does not do this, allow
  // for the possibility for completeness.
  bool si_code_valid = !(siginfo->si_code <= 0 ||
                         siginfo->si_code == SI_USER ||
                         siginfo->si_code == SI_QUEUE ||
                         siginfo->si_code == SI_TIMER ||
                         siginfo->si_code == SI_ASYNCIO ||
                         siginfo->si_code == SI_MESGQ);

  // 0x5343 = 'SC', signifying “signal and code”, disambiguates from the schema
  // used by ExceptionCodeForMetrics(). That system primarily uses Mach
  // exception types and codes, which are not available to a POSIX signal
  // handler. It does provide a way to encode only signal numbers, but does so
  // with the understanding that certain “raw” signals would not be encountered
  // without a Mach exception. Furthermore, it does not allow siginfo->si_code
  // to be encoded, because that’s not available to Mach exception handlers. It
  // would be a shame to lose that information available to a POSIX signal
  // handler.
  int metrics_code = 0x53430000 | (InRangeCast<uint8_t>(sig, 0xff) << 8);
  if (si_code_valid) {
    metrics_code |= InRangeCast<uint8_t>(siginfo->si_code, 0xff);
  }
  Metrics::HandlerCrashed(metrics_code);

  Signals::RestoreHandlerAndReraiseSignalOnReturn(siginfo, nullptr);
}

void HandleTerminateSignal(int sig, siginfo_t* siginfo, void* context) {
  MetricsRecordExit(Metrics::LifetimeMilestone::kTerminated);
  Signals::RestoreHandlerAndReraiseSignalOnReturn(siginfo, nullptr);
}

void ReinstallCrashHandler() {
  // This is used to re-enable the metrics-recording crash handler after
  // MonitorSelf() sets up a Crashpad exception handler. On macOS, the
  // metrics-recording handler uses signals and the Crashpad handler uses Mach
  // exceptions, so there’s nothing to re-enable.
  // On Linux, the signal handler installed by StartHandler() restores the
  // previously installed signal handler by default.
}

void InstallCrashHandler() {
  Signals::InstallCrashHandlers(HandleCrashSignal, 0, nullptr);

  // Not a crash handler, but close enough.
  Signals::InstallTerminateHandlers(HandleTerminateSignal, 0, nullptr);
}

#if BUILDFLAG(IS_APPLE)

struct ResetSIGTERMTraits {
  static struct sigaction* InvalidValue() {
    return nullptr;
  }

  static void Free(struct sigaction* sa) {
    int rv = sigaction(SIGTERM, sa, nullptr);
    PLOG_IF(ERROR, rv != 0) << "sigaction";
  }
};
using ScopedResetSIGTERM =
    base::ScopedGeneric<struct sigaction*, ResetSIGTERMTraits>;

ExceptionHandlerServer* g_exception_handler_server;

// This signal handler is only operative when being run from launchd.
void HandleSIGTERM(int sig, siginfo_t* siginfo, void* context) {
  // Don’t call MetricsRecordExit(). This is part of the normal exit path when
  // running from launchd.

  DCHECK(g_exception_handler_server);
  g_exception_handler_server->Stop();
}

#endif  // BUILDFLAG(IS_APPLE)

#endif  // BUILDFLAG(IS_APPLE)

void MonitorSelf(const Options& options) {
  base::FilePath executable_path;
  if (!Paths::Executable(&executable_path)) {
    return;
  }

  if (std::find(options.monitor_self_arguments.begin(),
                options.monitor_self_arguments.end(),
                "--monitor-self") != options.monitor_self_arguments.end()) {
    LOG(WARNING) << "--monitor-self-argument=--monitor-self is not supported";
    return;
  }
  std::vector<std::string> extra_arguments(options.monitor_self_arguments);
  if (!options.identify_client_via_url) {
    extra_arguments.push_back("--no-identify-client-via-url");
  }
  extra_arguments.push_back("--no-periodic-tasks");
  if (!options.rate_limit) {
    extra_arguments.push_back("--no-rate-limit");
  }
  if (!options.upload_gzip) {
    extra_arguments.push_back("--no-upload-gzip");
  }
  for (const auto& iterator : options.monitor_self_annotations) {
    extra_arguments.push_back(
        base::StringPrintf("--monitor-self-annotation=%s=%s",
                           iterator.first.c_str(),
                           iterator.second.c_str()));
  }

  // Don’t use options.metrics_dir. The current implementation only allows one
  // instance of crashpad_handler to be writing metrics at a time, and it should
  // be the primary instance.
  CrashpadClient crashpad_client;
  if (!crashpad_client.StartHandler(executable_path,
                                    options.database,
                                    base::FilePath(),
                                    options.url,
                                    options.annotations,
                                    extra_arguments,
                                    true,
                                    false)) {
    return;
  }

  // Make sure that appropriate metrics will be recorded on crash before this
  // process is terminated.
  ReinstallCrashHandler();
}

class ScopedStoppable {
 public:
  ScopedStoppable() = default;

  ScopedStoppable(const ScopedStoppable&) = delete;
  ScopedStoppable& operator=(const ScopedStoppable&) = delete;

  ~ScopedStoppable() {
    if (stoppable_) {
      stoppable_->Stop();
    }
  }

  void Reset(Stoppable* stoppable) { stoppable_.reset(stoppable); }

  Stoppable* Get() { return stoppable_.get(); }

 private:
  std::unique_ptr<Stoppable> stoppable_;
};

void InitCrashpadLogging() {
  logging::LoggingSettings settings;
  settings.logging_dest =
      logging::LOG_TO_SYSTEM_DEBUG_LOG | logging::LOG_TO_STDERR;
  logging::InitLogging(settings);
}

}  // namespace

int HandlerMain(int argc,
                char* argv[],
                const UserStreamDataSources* user_stream_sources) {
  InitCrashpadLogging();

  InstallCrashHandler();
  CallMetricsRecordNormalExit metrics_record_normal_exit;

  const base::FilePath argv0(
      ToolSupport::CommandLineArgumentToFilePathStringType(argv[0]));
  const base::FilePath me(argv0.BaseName());

  enum OptionFlags {
    // Long options without short equivalents.
    kOptionLastChar = 255,
    kOptionAnnotation,
#if defined(ATTACHMENTS_SUPPORTED)
    kOptionAttachment,
#endif  // defined(ATTACHMENTS_SUPPORTED)
    kOptionDatabase,
#if BUILDFLAG(IS_APPLE)
    kOptionHandshakeFD,
#endif  // BUILDFLAG(IS_APPLE)
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    kOptionInitialClientFD,
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS)
#if BUILDFLAG(IS_APPLE)
    kOptionMachService,
#endif  // BUILDFLAG(IS_APPLE)
    kOptionMetrics,
    kOptionMonitorSelf,
    kOptionMonitorSelfAnnotation,
    kOptionMonitorSelfArgument,
    kOptionNoIdentifyClientViaUrl,
    kOptionNoPeriodicTasks,
    kOptionNoRateLimit,
    kOptionNoUploadGzip,
#if BUILDFLAG(IS_APPLE)
    kOptionResetOwnCrashExceptionPortToSystemDefault,
#endif  // BUILDFLAG(IS_APPLE)
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
    kOptionSanitizationInformation,
    kOptionSharedClientConnection,
    kOptionTraceParentWithException,
#endif
    kOptionURL,
    kOptionMaxUploads,
    kOptionMaxDatabaseSize,
    kOptionMaxDatabaseAge,

    // Standard options.
    kOptionHelp = -2,
    kOptionVersion = -3,
  };

  static constexpr option long_options[] = {
    {"annotation", required_argument, nullptr, kOptionAnnotation},
#if defined(ATTACHMENTS_SUPPORTED)
    {"attachment", required_argument, nullptr, kOptionAttachment},
#endif  // ATTACHMENTS_SUPPORTED
    {"database", required_argument, nullptr, kOptionDatabase},
#if BUILDFLAG(IS_APPLE)
    {"handshake-fd", required_argument, nullptr, kOptionHandshakeFD},
#endif  // BUILDFLAG(IS_APPLE)
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    {"initial-client-fd", required_argument, nullptr, kOptionInitialClientFD},
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS)
#if BUILDFLAG(IS_APPLE)
    {"mach-service", required_argument, nullptr, kOptionMachService},
#endif  // BUILDFLAG(IS_APPLE)
    {"metrics-dir", required_argument, nullptr, kOptionMetrics},
    {"monitor-self", no_argument, nullptr, kOptionMonitorSelf},
    {"monitor-self-annotation",
     required_argument,
     nullptr,
     kOptionMonitorSelfAnnotation},
    {"monitor-self-argument",
     required_argument,
     nullptr,
     kOptionMonitorSelfArgument},
    {"no-identify-client-via-url",
     no_argument,
     nullptr,
     kOptionNoIdentifyClientViaUrl},
    {"no-periodic-tasks", no_argument, nullptr, kOptionNoPeriodicTasks},
    {"no-rate-limit", no_argument, nullptr, kOptionNoRateLimit},
    {"no-upload-gzip", no_argument, nullptr, kOptionNoUploadGzip},
#if BUILDFLAG(IS_APPLE)
    {"reset-own-crash-exception-port-to-system-default",
     no_argument,
     nullptr,
     kOptionResetOwnCrashExceptionPortToSystemDefault},
#endif  // BUILDFLAG(IS_APPLE)
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
    {"sanitization-information",
     required_argument,
     nullptr,
     kOptionSanitizationInformation},
    {"shared-client-connection",
     no_argument,
     nullptr,
     kOptionSharedClientConnection},
    {"trace-parent-with-exception",
     required_argument,
     nullptr,
     kOptionTraceParentWithException},
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) ||
        // BUILDFLAG(IS_ANDROID)
    {"url", required_argument, nullptr, kOptionURL},
    {"help", no_argument, nullptr, kOptionHelp},
    {"version", no_argument, nullptr, kOptionVersion},
    {"max-uploads", required_argument, nullptr, kOptionMaxUploads},
    {"max-db-size", required_argument, nullptr, kOptionMaxDatabaseSize},
    {"max-db-age", required_argument, nullptr, kOptionMaxDatabaseAge},
    {nullptr, 0, nullptr, 0},
  };

  Options options = {};
#if BUILDFLAG(IS_APPLE)
  options.handshake_fd = -1;
#endif
  options.identify_client_via_url = true;
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
  options.initial_client_fd = kInvalidFileHandle;
#endif
  options.periodic_tasks = true;
  options.rate_limit = true;
  options.upload_gzip = true;

  int opt;
  while ((opt = getopt_long(argc, argv, "", long_options, nullptr)) != -1) {
    switch (opt) {
      case kOptionAnnotation: {
        if (!AddKeyValueToMap(&options.annotations, optarg, "--annotation")) {
          return ExitFailure();
        }
        break;
      }
#if defined(ATTACHMENTS_SUPPORTED)
      case kOptionAttachment: {
        options.attachments.push_back(base::FilePath(
            ToolSupport::CommandLineArgumentToFilePathStringType(optarg)));
        break;
      }
#endif  // ATTACHMENTS_SUPPORTED
      case kOptionDatabase: {
        options.database = base::FilePath(
            ToolSupport::CommandLineArgumentToFilePathStringType(optarg));
        break;
      }
#if BUILDFLAG(IS_APPLE)
      case kOptionHandshakeFD: {
        if (!StringToNumber(optarg, &options.handshake_fd) ||
            options.handshake_fd < 0) {
          ToolSupport::UsageHint(me,
                                 "--handshake-fd requires a file descriptor");
          return ExitFailure();
        }
        break;
      }
      case kOptionMachService: {
        options.mach_service = optarg;
        break;
      }
#endif  // BUILDFLAG(IS_APPLE)
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
      case kOptionInitialClientFD: {
        if (!base::StringToInt(optarg, &options.initial_client_fd)) {
          ToolSupport::UsageHint(me, "failed to parse --initial-client-fd");
          return ExitFailure();
        }
        break;
      }
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_LINUX) ||
        // BUILDFLAG(IS_CHROMEOS)
      case kOptionMetrics: {
        options.metrics_dir = base::FilePath(
            ToolSupport::CommandLineArgumentToFilePathStringType(optarg));
        break;
      }
      case kOptionMonitorSelf: {
        options.monitor_self = true;
        break;
      }
      case kOptionMonitorSelfAnnotation: {
        if (!AddKeyValueToMap(&options.monitor_self_annotations,
                              optarg,
                              "--monitor-self-annotation")) {
          return ExitFailure();
        }
        break;
      }
      case kOptionMonitorSelfArgument: {
        options.monitor_self_arguments.push_back(optarg);
        break;
      }
      case kOptionNoIdentifyClientViaUrl: {
        options.identify_client_via_url = false;
        break;
      }
      case kOptionNoPeriodicTasks: {
        options.periodic_tasks = false;
        break;
      }
      case kOptionNoRateLimit: {
        options.rate_limit = false;
        break;
      }
      case kOptionNoUploadGzip: {
        options.upload_gzip = false;
        break;
      }
#if BUILDFLAG(IS_APPLE)
      case kOptionResetOwnCrashExceptionPortToSystemDefault: {
        options.reset_own_crash_exception_port_to_system_default = true;
        break;
      }
#endif  // BUILDFLAG(IS_APPLE)
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
      case kOptionSanitizationInformation: {
        if (!StringToNumber(optarg,
                            &options.sanitization_information_address)) {
          ToolSupport::UsageHint(me,
                                 "failed to parse --sanitization-information");
          return ExitFailure();
        }
        break;
      }
      case kOptionSharedClientConnection: {
        options.shared_client_connection = true;
        break;
      }
      case kOptionTraceParentWithException: {
        if (!StringToNumber(optarg, &options.exception_information_address)) {
          ToolSupport::UsageHint(
              me, "failed to parse --trace-parent-with-exception");
          return ExitFailure();
        }
        break;
      }
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) ||
        // BUILDFLAG(IS_ANDROID)
      case kOptionURL: {
        options.url = optarg;
        break;
      }
      case kOptionMaxUploads: {
        if (base::StringToInt(optarg, &options.max_uploads)) {
          if (options.max_uploads < 0)
            options.max_uploads = 0;
        }
        break;
      }
      case kOptionMaxDatabaseSize: {
        if (base::StringToInt(optarg, &options.max_database_size)) {
          if (options.max_database_size < 0)
            options.max_database_size = 0;
        }
        break;
      }
      case kOptionMaxDatabaseAge: {
        if (base::StringToInt(optarg, &options.max_database_age)) {
          if (options.max_database_age < 0)
            options.max_database_age = 0;
        }
        break;
      }
      case kOptionHelp: {
        Usage(me);
        MetricsRecordExit(Metrics::LifetimeMilestone::kExitedEarly);
        return EXIT_SUCCESS;
      }
      case kOptionVersion: {
        ToolSupport::Version(me);
        MetricsRecordExit(Metrics::LifetimeMilestone::kExitedEarly);
        return EXIT_SUCCESS;
      }
      default: {
        ToolSupport::UsageHint(me, nullptr);
        return ExitFailure();
      }
    }
  }
  argc -= optind;
  argv += optind;

#if BUILDFLAG(IS_APPLE)
  if (options.handshake_fd < 0 && options.mach_service.empty()) {
    ToolSupport::UsageHint(me, "--handshake-fd or --mach-service is required");
    return ExitFailure();
  }
  if (options.handshake_fd >= 0 && !options.mach_service.empty()) {
    ToolSupport::UsageHint(
        me, "--handshake-fd and --mach-service are incompatible");
    return ExitFailure();
  }
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
  if (!options.exception_information_address &&
      options.initial_client_fd == kInvalidFileHandle) {
    ToolSupport::UsageHint(
        me, "--trace-parent-with-exception or --initial-client-fd is required");
    return ExitFailure();
  }
  if (options.sanitization_information_address &&
      !options.exception_information_address) {
    ToolSupport::UsageHint(
        me,
        "--sanitization_information requires --trace-parent-with-exception");
    return ExitFailure();
  }
  if (options.shared_client_connection &&
      options.initial_client_fd == kInvalidFileHandle) {
    ToolSupport::UsageHint(
        me, "--shared-client-connection requires --initial-client-fd");
    return ExitFailure();
  }
#endif  // BUILDFLAG(IS_APPLE)

  if (options.database.empty()) {
    ToolSupport::UsageHint(me, "--database is required");
    return ExitFailure();
  }

  if (argc) {
    ToolSupport::UsageHint(me, nullptr);
    return ExitFailure();
  }

#if BUILDFLAG(IS_APPLE)
  if (options.reset_own_crash_exception_port_to_system_default) {
    CrashpadClient::UseSystemDefaultHandler();
  }
#endif  // BUILDFLAG(IS_APPLE)

  if (options.monitor_self) {
    MonitorSelf(options);
  }

  if (!options.monitor_self_annotations.empty()) {
    // Establish these annotations even if --monitor-self is not present, in
    // case something such as generate_dump wants to try to access them later.
    //
    // If the handler is part of a multi-purpose executable, simple annotations
    // may already be present for this module. If they are, use them.
    CrashpadInfo* crashpad_info = CrashpadInfo::GetCrashpadInfo();
    SimpleStringDictionary* module_annotations =
        crashpad_info->simple_annotations();
    if (!module_annotations) {
      module_annotations = new SimpleStringDictionary();
      crashpad_info->set_simple_annotations(module_annotations);
    }

    for (const auto& iterator : options.monitor_self_annotations) {
      module_annotations->SetKeyValue(iterator.first.c_str(),
                                      iterator.second.c_str());
    }
  }

  std::unique_ptr<CrashReportDatabase> database(
      CrashReportDatabase::Initialize(options.database));
  if (!database) {
    return ExitFailure();
  }

  ScopedStoppable upload_thread;
  if (!options.url.empty()) {
    // TODO(scottmg): options.rate_limit should be removed when we have a
    // configurable database setting to control upload limiting.
    // See https://crashpad.chromium.org/bug/23.
    CrashReportUploadThread::Options upload_thread_options;
    upload_thread_options.identify_client_via_url =
        options.identify_client_via_url;
    upload_thread_options.rate_limit = options.rate_limit;
    upload_thread_options.upload_gzip = options.upload_gzip;
    upload_thread_options.watch_pending_reports = options.periodic_tasks;

#if BUILDFLAG(ENABLE_CEF)
    upload_thread.Reset(new CefCrashReportUploadThread(
        database.get(),
        options.url,
        upload_thread_options,
        CrashReportUploadThread::ProcessPendingReportsObservationCallback(),
        options.max_uploads));
#else
    upload_thread.Reset(new CrashReportUploadThread(
        database.get(),
        options.url,
        upload_thread_options,
        CrashReportUploadThread::ProcessPendingReportsObservationCallback()));
#endif
    upload_thread.Get()->Start();
  }

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
  std::unique_ptr<ExceptionHandlerServer::Delegate> exception_handler;
#else
  std::unique_ptr<CrashReportExceptionHandler> exception_handler;
#endif

  exception_handler = std::make_unique<CrashReportExceptionHandler>(
      database.get(),
      static_cast<CrashReportUploadThread*>(upload_thread.Get()),
      &options.annotations,
#if defined(ATTACHMENTS_SUPPORTED)
      &options.attachments,
#endif  // ATTACHMENTS_SUPPORTED
#if BUILDFLAG(IS_LINUX)
      true,
      false,
#endif  // BUILDFLAG(IS_LINUX)
      user_stream_sources);

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
  if (options.exception_information_address) {
    ExceptionHandlerProtocol::ClientInformation info;
    info.exception_information_address = options.exception_information_address;
    info.sanitization_information_address =
        options.sanitization_information_address;
    return exception_handler->HandleException(getppid(), geteuid(), info)
               ? EXIT_SUCCESS
               : ExitFailure();
  }
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) ||
        // BUILDFLAG(IS_ANDROID)

  ScopedStoppable prune_thread;
  if (options.periodic_tasks) {
    prune_thread.Reset(new PruneCrashReportThread(
        database.get(), PruneCondition::GetDefault(options.max_database_size,
                                                   options.max_database_age)));
    prune_thread.Get()->Start();
  }

#if BUILDFLAG(IS_APPLE)
  if (options.mach_service.empty()) {
    // Don’t do this when being run by launchd. See launchd.plist(5).
    CloseStdinAndStdout();
  }

  base::apple::ScopedMachReceiveRight receive_right;

  if (options.handshake_fd >= 0) {
    receive_right.reset(
        ChildPortHandshake::RunServerForFD(
            base::ScopedFD(options.handshake_fd),
            ChildPortHandshake::PortRightType::kReceiveRight));
  } else if (!options.mach_service.empty()) {
    receive_right = BootstrapCheckIn(options.mach_service);
  }

  if (!receive_right.is_valid()) {
    return ExitFailure();
  }

  ExceptionHandlerServer exception_handler_server(
      std::move(receive_right), !options.mach_service.empty());
  base::AutoReset<ExceptionHandlerServer*> reset_g_exception_handler_server(
      &g_exception_handler_server, &exception_handler_server);

  struct sigaction old_sigterm_action;
  ScopedResetSIGTERM reset_sigterm;
  if (!options.mach_service.empty()) {
    // When running from launchd, no no-senders notification could ever be
    // triggered, because launchd maintains a send right to the service. When
    // launchd wants the job to exit, it will send a SIGTERM. See
    // launchd.plist(5).
    //
    // Set up a SIGTERM handler that will call exception_handler_server.Stop().
    // This replaces the HandleTerminateSignal handler for SIGTERM.
    if (Signals::InstallHandler(
            SIGTERM, HandleSIGTERM, 0, &old_sigterm_action)) {
      reset_sigterm.reset(&old_sigterm_action);
    }
  }

  RecordFileLimitAnnotation();
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
  ExceptionHandlerServer exception_handler_server;
#endif  // BUILDFLAG(IS_APPLE)

  base::GlobalHistogramAllocator* histogram_allocator = nullptr;
  if (!options.metrics_dir.empty()) {
    static constexpr char kMetricsName[] = "CrashpadMetrics";
    constexpr size_t kMetricsFileSize = 1 << 20;
    if (base::GlobalHistogramAllocator::CreateWithActiveFileInDir(
            options.metrics_dir, kMetricsFileSize, 0, kMetricsName)) {
      histogram_allocator = base::GlobalHistogramAllocator::Get();
      histogram_allocator->CreateTrackingHistograms(kMetricsName);
    }
  }

  Metrics::HandlerLifetimeMilestone(Metrics::LifetimeMilestone::kStarted);

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
  if (options.initial_client_fd == kInvalidFileHandle ||
      !exception_handler_server.InitializeWithClient(
          ScopedFileHandle(options.initial_client_fd),
          options.shared_client_connection)) {
    return ExitFailure();
  }
#endif  // BUILDFLAG(IS_WIN)

  exception_handler_server.Run(exception_handler.get());

  return EXIT_SUCCESS;
}

}  // namespace crashpad
