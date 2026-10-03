// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/crash/core/app/crashpad.h"

#include <stddef.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "base/base_paths.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/debug/crash_logging.h"
#include "base/debug/dump_without_crashing.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/system/sys_info.h"
#include "base/time/time.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "components/crash/core/app/crash_reporter_client.h"
#include "components/crash/core/common/crash_key.h"
#include "third_party/crashpad/crashpad/client/annotation.h"
#include "third_party/crashpad/crashpad/client/annotation_list.h"
#include "third_party/crashpad/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/crashpad/client/crashpad_client.h"
#include "third_party/crashpad/crashpad/client/crashpad_info.h"
#include "third_party/crashpad/crashpad/client/settings.h"
#include "third_party/crashpad/crashpad/client/simulate_crash.h"

#include <unistd.h>


namespace crash_reporter {


namespace {

base::FilePath* g_database_path;

crashpad::CrashReportDatabase* g_database;

void InitializeDatabasePath(const base::FilePath& database_path) {
  DCHECK(!g_database_path);

  // Intentionally leaked.
  g_database_path = new base::FilePath(database_path);
}

bool InitializeCrashpadImpl(bool initial_client,
                            const std::string& process_type,
                            const std::string& user_data_dir,
                            const base::FilePath& exe_path,
                            const std::vector<std::string>& initial_arguments,
                            bool embedded_handler,
                            const std::vector<base::FilePath>& attachments) {
  static bool initialized = false;
  DCHECK(!initialized);
  initialized = true;

  const bool browser_process = process_type.empty();

  if (initial_client) {
#if BUILDFLAG(IS_APPLE)
    // "relauncher" is hard-coded because it's a Chrome --type, but this
    // component can't see Chrome's switches. This is only used for argument
    // sanitization.
    DCHECK(browser_process || process_type == "relauncher" ||
           process_type == "app_shim");
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
    DCHECK(browser_process);
#else
#error Port.
#endif  // BUILDFLAG(IS_APPLE)
  } else {
    DCHECK(!browser_process);
  }

  // database_path is only valid in the browser process.
  base::FilePath database_path;
  if (!internal::PlatformCrashpadInitialization(
          initial_client, browser_process, embedded_handler, user_data_dir,
          exe_path, initial_arguments, attachments, &database_path)) {
    return false;
  }

#if BUILDFLAG(IS_APPLE)
#if defined(NDEBUG)
  const bool is_debug_build = false;
#else
  const bool is_debug_build = true;
#endif

  // Disable forwarding to the system's crash reporter in processes other than
  // the browser process. For the browser, the system's crash reporter presents
  // the crash UI to the user, so it's desirable there. Additionally, having
  // crash reports appear in ~/Library/Logs/DiagnosticReports provides a
  // fallback. Forwarding is turned off for debug-mode builds even for the
  // browser process, because the system's crash reporter can take a very long
  // time to chew on symbols.
  if (!browser_process || is_debug_build ||
      !GetCrashReporterClient()->EnableBrowserCrashForwarding()) {
    crashpad::CrashpadInfo::GetCrashpadInfo()
        ->set_system_crash_reporter_forwarding(crashpad::TriState::kDisabled);
  }
#endif  // BUILDFLAG(IS_APPLE)

  InitializeCrashKeys();
  static crashpad::StringAnnotation<24> ptype_key("ptype");
  ptype_key.Set(browser_process ? std::string_view("browser")
                                : std::string_view(process_type));

  static crashpad::StringAnnotation<12> pid_key("pid");
  pid_key.Set(base::NumberToString(getpid()));

  static crashpad::StringAnnotation<24> osarch_key("osarch");
  osarch_key.Set(base::SysInfo::OperatingSystemArchitecture());

  // If clients called CRASHPAD_SIMULATE_CRASH() instead of
  // base::debug::DumpWithoutCrashing(), these dumps would appear as crashes in
  // the correct function, at the correct file and line. This would be
  // preferable to having all occurrences show up in DumpWithoutCrashing() at
  // the same file and line.
  base::debug::SetDumpWithoutCrashingFunction(DumpWithoutCrashing);

#if BUILDFLAG(IS_APPLE)
  // On Mac, we only want the browser to initialize the database, but not the
  // relauncher.
  const bool should_initialize_database_and_set_upload_policy = browser_process;
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
  const bool should_initialize_database_and_set_upload_policy = browser_process;
#endif
  if (should_initialize_database_and_set_upload_policy) {
    InitializeDatabasePath(database_path);

    g_database =
        crashpad::CrashReportDatabase::Initialize(database_path).release();

#if !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_IOS) && !BUILDFLAG(IS_ANDROID)
    // On Android crashpad doesn't handle uploads. Android uses
    // //components/minidump_uploader which queries metrics sample/consent opt
    // in from preferences.
    CrashReporterClient* crash_reporter_client = GetCrashReporterClient();
    SetUploadConsent(crash_reporter_client->GetCollectStatsConsent());
#endif
  }
  return true;
}

}  // namespace

bool InitializeCrashpad(bool initial_client, const std::string& process_type) {
  return InitializeCrashpadImpl(initial_client, process_type, std::string(),
                                base::FilePath(), std::vector<std::string>(),
                                /*embedded_handler=*/false, /*attachments=*/{});
}


namespace {
crashpad::CrashpadClient* crashpad_client = nullptr;
} // namespace

crashpad::CrashpadClient& GetCrashpadClient() {
  if (!crashpad_client) {
    crashpad_client = new crashpad::CrashpadClient();
  }
  return *crashpad_client;
}

void DestroyCrashpadClient() {
  if (crashpad_client) {
    delete crashpad_client;
    crashpad_client = nullptr;
  }
}

void SetUploadConsent(bool consent) {
  if (!g_database)
    return;

  bool enable_uploads = false;
  CrashReporterClient* crash_reporter_client = GetCrashReporterClient();
  if (!crash_reporter_client->ReportingIsEnforcedByPolicy(&enable_uploads)) {
    // Breakpad provided a --disable-breakpad switch to disable crash dumping
    // (not just uploading) here. Crashpad doesn't need it: dumping is enabled
    // unconditionally and uploading is gated on consent, which tests/bots
    // shouldn't have. As a precaution, uploading is also disabled on bots even
    // if consent is present.
    enable_uploads = consent && !crash_reporter_client->IsRunningUnattended();
  }

  crashpad::Settings* settings = g_database->GetSettings();
  settings->SetUploadsEnabled(enable_uploads &&
                              crash_reporter_client->GetCollectStatsInSample());
}


void DumpWithoutCrashing() {
  CRASHPAD_SIMULATE_CRASH();
}



#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
void CrashWithoutDumping(const std::string& message) {
  crashpad::CrashpadClient::CrashWithoutDump(message);
}
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) ||
        // BUILDFLAG(IS_ANDROID)

void GetReports(std::vector<Report>* reports) {
  GetReportsImpl(reports);
}

void RequestSingleCrashUpload(const std::string& local_id) {
  crash_reporter::RequestSingleCrashUploadImpl(local_id);
}

std::optional<base::FilePath> GetCrashpadDatabasePath() {
  base::FilePath::StringType::const_pointer path =
      GetCrashpadDatabasePathImpl();
  if (!path) {
    return std::nullopt;
  }
  return base::FilePath(path);
}

void ClearReportsBetween(const base::Time& begin, const base::Time& end) {
  ClearReportsBetweenImpl(begin.ToTimeT(), end.ToTimeT());
}

void GetReportsImpl(std::vector<Report>* reports) {
  reports->clear();

  if (!g_database) {
    return;
  }

  std::vector<crashpad::CrashReportDatabase::Report> completed_reports;
  crashpad::CrashReportDatabase::OperationStatus status =
      g_database->GetCompletedReports(&completed_reports);
  if (status != crashpad::CrashReportDatabase::kNoError) {
    return;
  }

  std::vector<crashpad::CrashReportDatabase::Report> pending_reports;
  status = g_database->GetPendingReports(&pending_reports);
  if (status != crashpad::CrashReportDatabase::kNoError) {
    return;
  }

  for (const crashpad::CrashReportDatabase::Report& completed_report :
       completed_reports) {
    Report report = {};

    // TODO(siggi): CHECK that this fits?
    base::strlcpy(report.local_id, completed_report.uuid.ToString().c_str(),
                  sizeof(report.local_id));

    report.capture_time = completed_report.creation_time;
    base::strlcpy(report.remote_id, completed_report.id.c_str(),
                  sizeof(report.remote_id));
    if (completed_report.uploaded) {
      report.upload_time = completed_report.last_upload_attempt_time;
      report.state = ReportUploadState::Uploaded;
    } else {
      report.upload_time = 0;
      report.state = ReportUploadState::NotUploaded;
    }
    reports->push_back(report);
  }

  for (const crashpad::CrashReportDatabase::Report& pending_report :
       pending_reports) {
    Report report = {};
    base::strlcpy(report.local_id, pending_report.uuid.ToString().c_str(),
                  sizeof(report.local_id));
    report.capture_time = pending_report.creation_time;
    report.upload_time = 0;
    report.state = pending_report.upload_explicitly_requested
                       ? ReportUploadState::Pending_UserRequested
                       : ReportUploadState::Pending;
    reports->push_back(report);
  }

  std::sort(reports->begin(), reports->end(),
            [](const Report& a, const Report& b) {
              return a.capture_time > b.capture_time;
            });
}

void RequestSingleCrashUploadImpl(const std::string& local_id) {
  if (!g_database)
    return;
  crashpad::UUID uuid;
  uuid.InitializeFromString(local_id);
  g_database->RequestUpload(uuid);
}

base::FilePath::StringType::const_pointer GetCrashpadDatabasePathImpl() {
  if (!g_database_path)
    return nullptr;

  return g_database_path->value().c_str();
}

void ClearReportsBetweenImpl(time_t begin, time_t end) {
  std::vector<Report> reports;
  GetReports(&reports);
  for (const Report& report : reports) {
    // Delete if either time lies in the range, as they both reveal that the
    // browser was open.
    if ((begin <= report.capture_time && report.capture_time <= end) ||
        (begin <= report.upload_time && report.upload_time <= end)) {
      crashpad::UUID uuid;
      uuid.InitializeFromString(report.local_id);
      g_database->DeleteReport(uuid);
    }
  }
}

namespace internal {

crashpad::CrashReportDatabase* GetCrashReportDatabase() {
  return g_database;
}

void SetCrashReportDatabaseForTesting(  // IN-TEST
    crashpad::CrashReportDatabase* database,
    base::FilePath* database_path) {
  g_database = database;
  g_database_path = database_path;
}

}  // namespace internal

}  // namespace crash_reporter
