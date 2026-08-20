// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/feedback/system_logs/log_sources/chrome_internal_log_source.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_string_value_serializer.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/system/sys_info.h"
#include "base/time/time.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/chrome_content_browser_client_extensions_part.h"
#include "chrome/browser/google/google_brand.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sync/sync_service_factory.h"
#include "chrome/common/channel_info.h"
#include "components/feedback/system_logs/system_logs_source.h"
#include "components/prefs/pref_service.h"
#include "components/sync/service/sync_internals_util.h"
#include "components/sync/service/sync_service.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/gpu_data_manager.h"
#include "extensions/browser/api/power/power_api.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/api/power.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#include "gpu/config/gpu_finch_features.h"
#include "ui/display/types/display_constants.h"



#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"
#include "chrome/browser/updater/updater.h"
#endif

#include "base/base64.h"
#include "base/feature_list.h"
#include "components/variations/net/variations_command_line.h"

namespace system_logs {

namespace {

constexpr char kSyncDataKey[] = "about_sync_data";
constexpr char kExtensionsListKey[] = "extensions";
constexpr char kPowerApiListKey[] = "chrome.power extensions";
constexpr char kChromeVersionTag[] = "CHROME VERSION";
constexpr char kSkiaGraphiteStatusKey[] = "skia_graphite_status";

constexpr char kOsVersionTag[] = "OS VERSION";


#if (BUILDFLAG(IS_WIN) && BUILDFLAG(GOOGLE_CHROME_BRANDING)) || \
    BUILDFLAG(IS_MAC)
constexpr char kUpdateErrorCode[] = "update_error_code";
constexpr char kUpdateHresult[] = "update_hresult";
#endif

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
constexpr char kCpuArch[] = "cpu_arch";
#endif


std::string GetChromeVersionString() {
  // Version of the current running browser.
  std::string browser_version =
      chrome::GetVersionString(chrome::WithExtendedStable(true));

  return browser_version;
}

#if BUILDFLAG(IS_WIN) && BUILDFLAG(GOOGLE_CHROME_BRANDING)
// Returns true if the path identified by |key| with the PathService is a parent
// or ancestor of |child|.
bool IsParentOf(int key, const base::FilePath& child) {
  base::FilePath path;
  return base::PathService::Get(key, &path) && path.IsParent(child);
}

// Returns a string representing the overall install location of the browser.
// "Program Files" and "Program Files (x86)" are both considered "per-machine"
// locations (for all users), whereas anything in a user's local app data dir is
// considered a "per-user" location. This function returns an answer that gives,
// in essence, the broad category of location without checking that the browser
// is operating out of the exact expected install directory. It is interesting
// to know via feedback reports if updates are failing with
// CANNOT_UPGRADE_CHROME_IN_THIS_DIRECTORY, which checks the exact directory,
// yet the reported install_location is not "unknown".
std::string DetermineInstallLocation() {
  base::FilePath exe_path;

  if (base::PathService::Get(base::FILE_EXE, &exe_path)) {
    if (IsParentOf(base::DIR_PROGRAM_FILESX86, exe_path) ||
        IsParentOf(base::DIR_PROGRAM_FILES, exe_path)) {
      return "per-machine";
    }
    if (IsParentOf(base::DIR_LOCAL_APP_DATA, exe_path))
      return "per-user";
  }
  return "unknown";
}
#endif  // BUILDFLAG(IS_WIN) && BUILDFLAG(GOOGLE_CHROME_BRANDING)

#if BUILDFLAG(IS_MAC)
std::string MacCpuArchAsString() {
  switch (base::mac::GetCPUType()) {
    case base::mac::CPUType::kIntel:
      return "x86-64";
    case base::mac::CPUType::kTranslatedIntel:
      return "x86-64/translated";
    case base::mac::CPUType::kArm:
      return "arm64";
  }
}
#endif  // BUILDFLAG(IS_MAC)


}  // namespace

ChromeInternalLogSource::ChromeInternalLogSource()
    : SystemLogsSource("ChromeInternal") {
}

ChromeInternalLogSource::~ChromeInternalLogSource() = default;

void ChromeInternalLogSource::Fetch(SysLogsSourceCallback callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  DCHECK(!callback.is_null());

  auto response = std::make_unique<SystemLogsResponse>();
  response->emplace(kChromeVersionTag, GetChromeVersionString());

  // On ChromeOS, this will be pulled in from the LSB_RELEASE.
  std::string os_version = base::SysInfo::OperatingSystemName() + ": " +
                           base::SysInfo::OperatingSystemVersion();
  response->emplace(kOsVersionTag, os_version);

  PopulateSyncLogs(response.get());
  PopulateExtensionInfoLogs(response.get());
  PopulatePowerApiLogs(response.get());
  if (base::FeatureList::IsEnabled(variations::kFeedbackIncludeVariations)) {
    PopulateVariations(response.get());
  }
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
  PopulateLastUpdateState(response.get());
#endif

#if BUILDFLAG(IS_MAC)
  response->emplace(kCpuArch, MacCpuArchAsString());
#endif

  std::string skia_graphite_status = "unknown";
  if (content::GpuDataManager::GetInstance()->IsEssentialGpuInfoAvailable()) {
    if (content::GpuDataManager::GetInstance()->GetFeatureStatus(
            gpu::GPU_FEATURE_TYPE_SKIA_GRAPHITE) ==
        gpu::kGpuFeatureStatusEnabled) {
      skia_graphite_status = "enabled";
    } else {
      skia_graphite_status = "disabled";
    }
  }
  response->emplace(kSkiaGraphiteStatusKey, skia_graphite_status);

  if (ProfileManager::GetLastUsedProfile()->IsChild())
    response->emplace("account_type", "child");

  // On other platforms, we're done. Invoke the callback.
  std::move(callback).Run(std::move(response));
}

void ChromeInternalLogSource::PopulateSyncLogs(SystemLogsResponse* response) {
  // Get logs for the last used profile since there is no notion of primary
  // profile.
  Profile* profile = ProfileManager::GetLastUsedProfile();
  if (!profile || !SyncServiceFactory::HasSyncService(profile))
    return;

  // Add sync logs to |response|.
  base::DictValue sync_logs = syncer::sync_ui_util::ConstructAboutInformation(
      syncer::sync_ui_util::IncludeSensitiveData(false),
      SyncServiceFactory::GetForProfile(profile),
      chrome::GetChannelName(chrome::WithExtendedStable(true)));
  std::string serialized_sync_logs;
  JSONStringValueSerializer(&serialized_sync_logs).Serialize(sync_logs);
  response->emplace(kSyncDataKey, serialized_sync_logs);
}

void ChromeInternalLogSource::PopulateExtensionInfoLogs(
    SystemLogsResponse* response) {
  Profile* profile = ProfileManager::GetLastUsedProfile();
  if (!profile)
    return;

  extensions::ExtensionRegistry* extension_registry =
      extensions::ExtensionRegistry::Get(profile);
  std::string extensions_list;
  for (const scoped_refptr<const extensions::Extension>& extension :
       extension_registry->enabled_extensions()) {
    // Format the list as:
    // "extension_id" : "extension_name" : "extension_version".

    // Work around the anonymizer tool recognizing some versions as IPv4s.
    // Replaces dots "." by underscores "_".
    // We shouldn't change the anonymizer tool as it is working as intended; it
    // must err on the side of safety.
    std::string version;
    base::ReplaceChars(extension->VersionString(), ".", "_", &version);
    extensions_list += extension->id() + " : " + extension->name() +
                       " : version " + version + "\n";
  }

  if (!extensions_list.empty())
    response->emplace(kExtensionsListKey, extensions_list);
}

void ChromeInternalLogSource::PopulatePowerApiLogs(
    SystemLogsResponse* response) {
  std::string info;
  for (auto* profile :
       g_browser_process->profile_manager()->GetLoadedProfiles()) {
    // Some profiles cannot have extensions, such as the System Profile.
    if (extensions::ChromeContentBrowserClientExtensionsPart::
            AreExtensionsDisabledForProfile(profile)) {
      continue;
    }

    for (const auto& it :
         extensions::PowerAPI::Get(profile)->extension_levels()) {
      if (!info.empty())
        info += ",\n";
      info += it.first + ": " + extensions::api::power::ToString(it.second);
    }
  }

  if (!info.empty())
    response->emplace(kPowerApiListKey, info);
}

void ChromeInternalLogSource::PopulateVariations(SystemLogsResponse* response) {
  std::vector<uint8_t> ciphertext;
  auto status =
      variations::VariationsCommandLine::GetForCurrentProcess().EncryptToString(
          &ciphertext);
  if (status == variations::VariationsStateEncryptionStatus::kSuccess) {
    std::string base64_encoded =
        base::Base64Encode(std::string(ciphertext.begin(), ciphertext.end()));
    response->emplace("variations", base64_encoded);
  }
}



#if BUILDFLAG(IS_MAC)
void ChromeInternalLogSource::PopulateLastUpdateState(
    SystemLogsResponse* response) {
  const std::optional<updater::mojom::UpdateState> update_state =
      updater::GetLastOnDemandUpdateState();
  if (!update_state) {
    return;  // There is nothing to include if no update check has completed.
  }
  response->emplace(
      kUpdateErrorCode,
      base::StrCat(
          {base::NumberToString(static_cast<int>(update_state->error_category)),
           "/", base::NumberToString(update_state->error_code)}));
  // `extra_code1` is not an HRESULT on macOS, but has similar semantics.
  response->emplace(kUpdateHresult,
                    base::NumberToString(update_state->extra_code1));
}
#endif  // BUILDFLAG(IS_MAC)

}  // namespace system_logs
