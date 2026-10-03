// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/external_provider_impl.h"

#include <stddef.h>

#include <algorithm>
#include <memory>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/metrics/field_trial.h"
#include "base/strings/string_util.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "base/version.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/app_mode/app_mode_utils.h"
#include "chrome/browser/browser_features.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/extensions/extension_management.h"
#include "chrome/browser/extensions/extension_migrator.h"
#include "chrome/browser/extensions/external_component_loader.h"
#include "chrome/browser/extensions/external_policy_loader.h"
#include "chrome/browser/extensions/external_pref_loader.h"
#include "chrome/browser/extensions/forced_extensions/install_stage_tracker_factory.h"
#include "chrome/browser/extensions/initial_external_extension_loader.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/pref_names.h"
#include "components/crx_file/id_util.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/external_install_info.h"
#include "extensions/browser/external_provider_interface.h"
#include "extensions/browser/forced_extensions/install_stage_tracker.h"
#include "extensions/browser/pref_names.h"
#include "extensions/buildflags/buildflags.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest.h"
#include "ui/base/l10n/l10n_util.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "chrome/browser/extensions/preinstalled_apps.h"
#include "chrome/browser/web_applications/preinstalled_app_install_features.h"
#endif



static_assert(BUILDFLAG(ENABLE_EXTENSIONS_CORE));

using content::BrowserThread;
using extensions::mojom::ManifestLocation;

namespace extensions {

namespace {


}  // namespace

// Constants for keeping track of extension preferences in a dictionary.
const char ExternalProviderImpl::kInstallParam[] = "install_parameter";
const char ExternalProviderImpl::kExternalCrx[] = "external_crx";
const char ExternalProviderImpl::kExternalVersion[] = "external_version";
const char ExternalProviderImpl::kExternalUpdateUrl[] = "external_update_url";
const char ExternalProviderImpl::kIsBookmarkApp[] = "is_bookmark_app";
const char ExternalProviderImpl::kIsFromWebstore[] = "is_from_webstore";
const char ExternalProviderImpl::kKeepIfPresent[] = "keep_if_present";
const char ExternalProviderImpl::kWasInstalledByOem[] = "was_installed_by_oem";
const char ExternalProviderImpl::kWebAppMigrationFlag[] =
    "web_app_migration_flag";
const char ExternalProviderImpl::kSupportedLocales[] = "supported_locales";
const char ExternalProviderImpl::kMayBeUntrusted[] = "may_be_untrusted";
const char ExternalProviderImpl::kMinProfileCreatedByVersion[] =
    "min_profile_created_by_version";
const char ExternalProviderImpl::kDoNotInstallForEnterprise[] =
    "do_not_install_for_enterprise";

ExternalProviderImpl::ExternalProviderImpl(
    VisitorInterface* service,
    const scoped_refptr<ExternalLoader>& loader,
    Profile* profile,
    ManifestLocation crx_location,
    ManifestLocation download_location,
    int creation_flags)
    : crx_location_(crx_location),
      download_location_(download_location),
      service_(service),
      loader_(loader),
      profile_(profile),
      creation_flags_(creation_flags) {
  DCHECK(profile_);
  loader_->Init(this);
}

ExternalProviderImpl::~ExternalProviderImpl() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  loader_->OwnerShutdown();
}

void ExternalProviderImpl::VisitRegisteredExtension() {
  // The loader will call back to SetPrefs.
  loader_->StartLoading();
}

void ExternalProviderImpl::SetPrefs(base::DictValue prefs) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Check if the service is still alive. It is possible that it went
  // away while |loader_| was working on the FILE thread.
  if (!service_)
    return;

  InstallStageTracker* install_stage_tracker =
      InstallStageTrackerFactory::GetForBrowserContext(profile_);
  for (auto it : prefs) {
    install_stage_tracker->ReportInstallCreationStage(
        it.first,
        InstallStageTracker::InstallCreationStage::SEEN_BY_EXTERNAL_PROVIDER);
  }

  prefs_ = std::move(prefs);
  ready_ = true;  // Queries for extensions are allowed from this point.

  NotifyServiceOnExternalExtensionsFound();
}

void ExternalProviderImpl::TriggerOnExternalExtensionFound() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Check if the service is still alive. It is possible that it went
  // away while |loader_| was working on the FILE thread. The prefs can be
  // missing if SetPrefs() was not called yet.
  if (!service_ || !prefs_)
    return;

  NotifyServiceOnExternalExtensionsFound();
}

void ExternalProviderImpl::NotifyServiceOnExternalExtensionsFound() {
  std::vector<ExternalInstallInfoUpdateUrl> external_update_url_extensions;
  std::vector<ExternalInstallInfoFile> external_file_extensions;

  RetrieveExtensionsFromPrefs(&external_update_url_extensions,
                              &external_file_extensions);
  for (const auto& extension : external_update_url_extensions)
    service_->OnExternalExtensionUpdateUrlFound(extension,
                                                /*force_update=*/true);

  for (const auto& extension : external_file_extensions)
    service_->OnExternalExtensionFileFound(extension);

  service_->OnExternalProviderReady(this);
}

void ExternalProviderImpl::UpdatePrefs(base::DictValue prefs) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  CHECK(allow_updates_);

  // Check if the service is still alive. It is possible that it went
  // away while |loader_| was working on the FILE thread.
  if (!service_)
    return;

  std::set<std::string> removed_extensions;
  // Find extensions that were removed by this ExternalProvider.
  for (auto pref : *prefs_) {
    const std::string& extension_id = pref.first;
    // Don't bother about invalid ids.
    if (!crx_file::id_util::IdIsValid(extension_id))
      continue;
    if (!prefs.Find(extension_id))
      removed_extensions.insert(extension_id);
  }

  *prefs_ = std::move(prefs);

  std::vector<ExternalInstallInfoUpdateUrl> external_update_url_extensions;
  std::vector<ExternalInstallInfoFile> external_file_extensions;
  RetrieveExtensionsFromPrefs(&external_update_url_extensions,
                              &external_file_extensions);

  // Notify ExtensionService about completion of finding incremental updates
  // from this provider.
  // Provide the list of added and removed extensions.
  service_->OnExternalProviderUpdateComplete(
      this, external_update_url_extensions, external_file_extensions,
      removed_extensions);
}

void ExternalProviderImpl::RetrieveExtensionsFromPrefs(
    std::vector<ExternalInstallInfoUpdateUrl>* external_update_url_extensions,
    std::vector<ExternalInstallInfoFile>* external_file_extensions) {
  // Set of unsupported extensions that need to be deleted from prefs_.
  std::set<std::string> unsupported_extensions;
  InstallStageTracker* install_stage_tracker =
      InstallStageTrackerFactory::GetForBrowserContext(profile_);

  // Discover all the extensions this provider has.
  for (auto pref : *prefs_) {
    const std::string& extension_id = pref.first;


    if (!crx_file::id_util::IdIsValid(extension_id)) {
      LOG(WARNING) << "Malformed extension dictionary: key "
                   << extension_id.c_str() << " is not a valid id.";
      install_stage_tracker->ReportFailure(
          extension_id, InstallStageTracker::FailureReason::INVALID_ID);
      continue;
    }

    if (!pref.second.is_dict()) {
      LOG(WARNING) << "Malformed extension dictionary: key "
                   << extension_id.c_str()
                   << " has a value that is not a dictionary.";
      install_stage_tracker->ReportFailure(
          extension_id,
          InstallStageTracker::FailureReason::MALFORMED_EXTENSION_DICT);
      continue;
    }

    const base::DictValue& extension_dict = pref.second.GetDict();
    const std::string* external_crx = extension_dict.FindString(kExternalCrx);
    std::string external_version;
    const std::string* external_update_url = nullptr;

    const base::Value* external_version_value =
        extension_dict.Find(kExternalVersion);
    if (external_version_value) {
      if (external_version_value->is_string()) {
        external_version = external_version_value->GetString();
      } else {
        install_stage_tracker->ReportFailure(
            extension_id, InstallStageTracker::FailureReason::
                              MALFORMED_EXTENSION_DICT_VERSION);
        LOG(WARNING) << "Malformed extension dictionary for extension: "
                     << extension_id.c_str() << ". " << kExternalVersion
                     << " value must be a string.";
        continue;
      }
    }

    external_update_url = extension_dict.FindString(kExternalUpdateUrl);
    if ((external_crx != nullptr) != (external_version_value != nullptr)) {
      install_stage_tracker->ReportFailure(
          extension_id,
          InstallStageTracker::FailureReason::MALFORMED_EXTENSION_DICT);
      LOG(WARNING) << "Malformed extension dictionary for extension: "
                   << extension_id.c_str() << ".  " << kExternalCrx
                   << " and " << kExternalVersion << " must be used together.";
      continue;
    }

    if ((external_crx != nullptr) == (external_update_url != nullptr)) {
      install_stage_tracker->ReportFailure(
          extension_id,
          InstallStageTracker::FailureReason::MALFORMED_EXTENSION_DICT);
      LOG(WARNING) << "Malformed extension dictionary for extension: "
                   << extension_id.c_str() << ".  Exactly one of the "
                   << "followng keys should be used: " << kExternalCrx
                   << ", " << kExternalUpdateUrl << ".";
      continue;
    }

    // Check that extension supports current browser locale.
    const base::ListValue* supported_locales =
        extension_dict.FindList(kSupportedLocales);
    if (supported_locales) {
      std::vector<std::string> browser_locales = l10n_util::GetParentLocales(
          g_browser_process->GetApplicationLocale());

      bool locale_supported = false;
      for (const base::Value& locale : *supported_locales) {
        const std::string* current_locale = locale.GetIfString();
        if (current_locale && l10n_util::IsValidLocaleSyntax(*current_locale)) {
          std::string normalized_locale =
              l10n_util::NormalizeLocale(*current_locale);
          if (std::ranges::contains(browser_locales, normalized_locale)) {
            locale_supported = true;
            break;
          }
        } else {
          LOG(WARNING) << "Unrecognized locale '"
                       << (current_locale ? *current_locale : "(Not a string)")
                       << "' found as supported locale for extension: "
                       << extension_id;
        }
      }

      if (!locale_supported) {
        unsupported_extensions.insert(extension_id);
        install_stage_tracker->ReportFailure(
            extension_id,
            InstallStageTracker::FailureReason::LOCALE_NOT_SUPPORTED);
        VLOG(1) << "Skip installing (or uninstall) external extension: "
                << extension_id << " because the extension doesn't support "
                << "the browser locale.";
        continue;
      }
    }

    int creation_flags = creation_flags_;
    std::optional<bool> is_from_webstore =
        extension_dict.FindBool(kIsFromWebstore);
    if (is_from_webstore.value_or(false)) {
      creation_flags |= Extension::FROM_WEBSTORE;
    }

    std::optional<bool> is_bookmark_app =
        extension_dict.FindBool(kIsBookmarkApp);
    if (is_bookmark_app.value_or(false)) {
      // Bookmark apps are obsolete, ignore any remaining dregs that haven't
      // already been migrated.
      continue;
    }

    // If the extension is in a web app migration treat it as "keep_if_present"
    // so it can get uninstalled by WebAppUiManager::UninstallAndReplace() once
    // the replacement web app has installed and migrated over user preferences.
    // TODO(crbug.com/1099150): Remove this field after migration is complete.
    // TODO(crbug.com/409795200): Decide how to handle this on desktop Android.
    // We can't currently depend on //chrome/browser/web_applications.
#if BUILDFLAG(ENABLE_EXTENSIONS)
    const std::string* web_app_migration_flag =
        extension_dict.FindString(kWebAppMigrationFlag);
    bool is_migrating_to_web_app =
        web_app_migration_flag &&
        web_app::IsPreinstalledAppInstallFeatureEnabled(
            *web_app_migration_flag);
#else
    bool is_migrating_to_web_app = false;
#endif
    bool keep_if_present =
        extension_dict.FindBool(kKeepIfPresent).value_or(false);
    if (keep_if_present || is_migrating_to_web_app) {
      ExtensionRegistry* extension_registry = ExtensionRegistry::Get(profile_);
      const Extension* extension =
          extension_registry ? extension_registry->GetExtensionById(
                                   extension_id, ExtensionRegistry::EVERYTHING)
                             : nullptr;
      if (!extension) {
        unsupported_extensions.insert(extension_id);
        install_stage_tracker->ReportFailure(
            extension_id,
            InstallStageTracker::FailureReason::NOT_PERFORMING_NEW_INSTALL);
        VLOG(1) << "Skip installing (or uninstall) external extension: "
                << extension_id << " because the extension should be kept "
                << "only if it is already installed.";
        continue;
      }
    }

    std::optional<bool> was_installed_by_oem =
        extension_dict.FindBool(kWasInstalledByOem);
    if (was_installed_by_oem.value_or(false)) {
      creation_flags |= Extension::WAS_INSTALLED_BY_OEM;
    }
    std::optional<bool> may_be_untrusted =
        extension_dict.FindBool(kMayBeUntrusted);
    if (may_be_untrusted.value_or(false)) {
      creation_flags |= Extension::MAY_BE_UNTRUSTED;
    }

    if (!HandleMinProfileVersion(extension_dict, extension_id,
                                 &unsupported_extensions)) {
      continue;
    }

    if (!HandleDoNotInstallForEnterprise(extension_dict, extension_id,
                                         &unsupported_extensions)) {
      continue;
    }

    const std::string* install_parameter =
        extension_dict.FindString(kInstallParam);

    if (external_crx) {
      if (crx_location_ == ManifestLocation::kInvalidLocation) {
        install_stage_tracker->ReportFailure(
            extension_id,
            InstallStageTracker::FailureReason::NOT_SUPPORTED_EXTENSION_DICT);
        LOG(WARNING) << "This provider does not support installing external "
                     << "extensions from crx files.";
        continue;
      }

      base::FilePath path = base::FilePath::FromUTF8Unsafe(*external_crx);
      if (path.value().find(base::FilePath::kParentDirectory) !=
          std::string_view::npos) {
        install_stage_tracker->ReportFailure(
            extension_id, InstallStageTracker::FailureReason::
                              MALFORMED_EXTENSION_DICT_FILE_PATH);
        LOG(WARNING) << "Path traversal not allowed in path: "
                     << external_crx->c_str();
        continue;
      }

      // If the path is relative, and the provider has a base path,
      // build the absolute path to the crx file.

      if (!path.IsAbsolute()) {
        base::FilePath base_path = loader_->GetBaseCrxFilePath();
        if (base_path.empty()) {
          install_stage_tracker->ReportFailure(
              extension_id, InstallStageTracker::FailureReason::
                                MALFORMED_EXTENSION_DICT_FILE_PATH);
          LOG(WARNING) << "File path " << external_crx->c_str()
                       << " is relative.  An absolute path is required.";
          continue;
        }
        path = base_path.Append(path);
      }

      base::Version version(external_version);
      if (!version.IsValid()) {
        install_stage_tracker->ReportFailure(
            extension_id, InstallStageTracker::FailureReason::
                              MALFORMED_EXTENSION_DICT_VERSION);
        LOG(WARNING) << "Malformed extension dictionary for extension: "
                     << extension_id.c_str() << ".  Invalid version string \""
                     << external_version << "\".";
        continue;
      }
      external_file_extensions->emplace_back(
          extension_id, version, path, crx_location_, creation_flags,
          auto_acknowledge_, install_immediately_);
    } else {                       // if (external_update_url)
      CHECK(external_update_url);  // Checking of keys above ensures this.
      if (download_location_ == ManifestLocation::kInvalidLocation) {
        install_stage_tracker->ReportFailure(
            extension_id,
            InstallStageTracker::FailureReason::NOT_SUPPORTED_EXTENSION_DICT);
        LOG(WARNING) << "This provider does not support installing external "
                     << "extensions from update URLs.";
        continue;
      }
      GURL update_url(*external_update_url);
      if (!update_url.is_valid()) {
        install_stage_tracker->ReportFailure(
            extension_id, InstallStageTracker::FailureReason::
                              MALFORMED_EXTENSION_DICT_UPDATE_URL);
        LOG(WARNING) << "Malformed extension dictionary for extension: "
                     << extension_id.c_str() << ".  Key " << kExternalUpdateUrl
                     << " has value \"" << *external_update_url
                     << "\", which is not a valid URL.";
        continue;
      }
      external_update_url_extensions->emplace_back(
          extension_id, install_parameter != nullptr ? *install_parameter : "",
          std::move(update_url), download_location_, creation_flags,
          auto_acknowledge_);
    }
  }

  for (auto it = unsupported_extensions.begin();
       it != unsupported_extensions.end(); ++it) {
    // Remove extension for the list of know external extensions. The extension
    // will be uninstalled later because provider doesn't provide it anymore.
    prefs_->Remove(*it);
  }
}

void ExternalProviderImpl::ServiceShutdown() {
  service_ = nullptr;
}

bool ExternalProviderImpl::IsReady() const {
  return ready_;
}

bool ExternalProviderImpl::HasExtension(
    const std::string& id) const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  CHECK(prefs_);
  CHECK(ready_);
  return prefs_->contains(id);
}

bool ExternalProviderImpl::HasExtensionWithLocation(
    const std::string& id,
    mojom::ManifestLocation location) const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  CHECK(prefs_);
  CHECK(ready_);
  const base::DictValue* dict = prefs_->FindDict(id);
  if (!dict) {
    return false;
  }

  if (dict->contains(kExternalUpdateUrl) && location == download_location_) {
    return true;
  }

  if (dict->contains(kExternalCrx) && dict->FindString(kExternalVersion) &&
      location == crx_location_) {
    return true;
  }

  return false;
}

bool ExternalProviderImpl::GetExtensionDetails(
    const std::string& id,
    ManifestLocation* location,
    std::unique_ptr<base::Version>* version) const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  CHECK(prefs_);
  CHECK(ready_);
  const base::DictValue* dict = prefs_->FindDict(id);
  if (!dict)
    return false;

  ManifestLocation loc = ManifestLocation::kInvalidLocation;
  if (dict->contains(kExternalUpdateUrl)) {
    loc = download_location_;

  } else if (dict->contains(kExternalCrx)) {
    loc = crx_location_;

    const std::string* external_version = dict->FindString(kExternalVersion);
    if (!external_version)
      return false;

    if (version)
      *version = std::make_unique<base::Version>(*external_version);

  } else {
    NOTREACHED();  // Chrome should not allow prefs to get into this state.
  }

  if (location)
    *location = loc;

  return true;
}

bool ExternalProviderImpl::HandleMinProfileVersion(
    const base::DictValue& extension,
    const std::string& extension_id,
    std::set<std::string>* unsupported_extensions) {
  const std::string* min_profile_created_by_version =
      extension.FindString(kMinProfileCreatedByVersion);
  if (min_profile_created_by_version) {
    base::Version profile_version(
        profile_->GetPrefs()->GetString(prefs::kProfileCreatedByVersion));
    base::Version min_version(*min_profile_created_by_version);
    if (min_version.IsValid() && profile_version.CompareTo(min_version) < 0) {
      unsupported_extensions->insert(extension_id);
      InstallStageTrackerFactory::GetForBrowserContext(profile_)->ReportFailure(
          extension_id, InstallStageTracker::FailureReason::TOO_OLD_PROFILE);
      VLOG(1) << "Skip installing (or uninstall) external extension: "
              << extension_id
              << " profile.created_by_version: " << profile_version.GetString()
              << " min_profile_created_by_version: "
              << *min_profile_created_by_version;
      return false;
    }
  }
  return true;
}

bool ExternalProviderImpl::HandleDoNotInstallForEnterprise(
    const base::DictValue& extension,
    const std::string& extension_id,
    std::set<std::string>* unsupported_extensions) {
  std::optional<bool> do_not_install_for_enterprise =
      extension.FindBool(kDoNotInstallForEnterprise);
  if (do_not_install_for_enterprise.value_or(false)) {
    const policy::ProfilePolicyConnector* const connector =
        profile_->GetProfilePolicyConnector();
    if (connector->IsManaged()) {
      unsupported_extensions->insert(extension_id);
      InstallStageTrackerFactory::GetForBrowserContext(profile_)->ReportFailure(
          extension_id,
          InstallStageTracker::FailureReason::DO_NOT_INSTALL_FOR_ENTERPRISE);
      VLOG(1) << "Skip installing (or uninstall) external extension "
              << extension_id << " restricted for managed user";
      return false;
    }
  }
  return true;
}

// static
void ExternalProviderImpl::CreateExternalProviders(
    VisitorInterface* service,
    Profile* profile,
    ProviderCollection* provider_list) {
  TRACE_EVENT0("browser,startup",
               "ExternalProviderImpl::CreateExternalProviders");
  scoped_refptr<ExternalLoader> external_loader;
  scoped_refptr<ExternalLoader> external_recommended_loader;
  [[maybe_unused]] ManifestLocation crx_location =
      ManifestLocation::kInvalidLocation;


  if (!external_loader.get()) {
    external_loader = base::MakeRefCounted<ExternalPolicyLoader>(
        profile, ExtensionManagementFactory::GetForBrowserContext(profile),
        ExternalPolicyLoader::FORCED);
    external_recommended_loader = base::MakeRefCounted<ExternalPolicyLoader>(
        profile, ExtensionManagementFactory::GetForBrowserContext(profile),
        ExternalPolicyLoader::RECOMMENDED);
  }

  // Policies are mandatory so they can't be skipped with command line flag.
  auto policy_provider = std::make_unique<ExternalProviderImpl>(
      service, external_loader, profile, crx_location,
      ManifestLocation::kExternalPolicyDownload, Extension::NO_FLAGS);
  policy_provider->set_allow_updates(true);
  provider_list->push_back(std::move(policy_provider));

  // Load the KioskAppExternalProvider when running in the Chrome App kiosk
  // mode.
  if (IsRunningInForcedAppMode()) {
    return;
  }

  // Extensions provided by recommended policies.
  if (external_recommended_loader.get()) {
    auto recommended_provider = std::make_unique<ExternalProviderImpl>(
        service, external_recommended_loader, profile, crx_location,
        ManifestLocation::kExternalPrefDownload, Extension::NO_FLAGS);
    recommended_provider->set_auto_acknowledge(true);
    provider_list->push_back(std::move(recommended_provider));
  }

  // In tests don't install pre-installed apps.
  // It would only slowdown tests and make them flaky.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          ::switches::kDisableDefaultApps)) {
    return;
  }

  // On Mac OS, items in /Library/... should be written by the superuser.
  // Check that all components of the path are writable by root only.
  ExternalPrefLoader::Options check_admin_permissions_on_mac;
#if BUILDFLAG(IS_MAC)
  check_admin_permissions_on_mac =
      ExternalPrefLoader::ENSURE_PATH_CONTROLLED_BY_ADMIN;
#else
  check_admin_permissions_on_mac = ExternalPrefLoader::NONE;
#endif
  int bundled_extension_creation_flags = Extension::NO_FLAGS;
  if (!profile->GetPrefs()->GetBoolean(pref_names::kBlockExternalExtensions)) {
#if BUILDFLAG(IS_LINUX)
    provider_list->push_back(std::make_unique<ExternalProviderImpl>(
        service,
        base::MakeRefCounted<ExternalPrefLoader>(
            chrome::DIR_STANDALONE_EXTERNAL_EXTENSIONS,
            ExternalPrefLoader::USE_USER_TYPE_PROFILE_FILTER, profile),
        profile, ManifestLocation::kExternalPref,
        ManifestLocation::kExternalPrefDownload,
        bundled_extension_creation_flags));
#endif
    provider_list->push_back(std::make_unique<ExternalProviderImpl>(
        service,
        base::MakeRefCounted<ExternalPrefLoader>(
            chrome::DIR_EXTERNAL_EXTENSIONS, check_admin_permissions_on_mac,
            nullptr),
        profile, ManifestLocation::kExternalPref,
        ManifestLocation::kExternalPrefDownload,
        bundled_extension_creation_flags));

    // Define a per-user source of external extensions.
#if BUILDFLAG(IS_MAC) || ((BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)) && \
                          BUILDFLAG(CHROMIUM_BRANDING))
    provider_list->push_back(std::make_unique<ExternalProviderImpl>(
        service,
        base::MakeRefCounted<ExternalPrefLoader>(
            chrome::DIR_USER_EXTERNAL_EXTENSIONS, ExternalPrefLoader::NONE,
            nullptr),
        profile, ManifestLocation::kExternalPref,
        ManifestLocation::kExternalPrefDownload, Extension::NO_FLAGS));
#endif
  }

#if !BUILDFLAG(IS_CHROMEOS) && BUILDFLAG(ENABLE_EXTENSIONS)
  // The pre-installed apps are installed as INTERNAL but use the external
  // extension installer codeflow.
  provider_list->push_back(std::make_unique<preinstalled_apps::Provider>(
      profile, service,
      base::MakeRefCounted<ExternalPrefLoader>(
          chrome::DIR_DEFAULT_APPS, ExternalPrefLoader::NONE, nullptr),
      ManifestLocation::kInternal, ManifestLocation::kInternal,
      Extension::FROM_WEBSTORE | Extension::WAS_INSTALLED_BY_DEFAULT));
#endif

  std::unique_ptr<ExternalProviderImpl> drive_migration_provider(
      new ExternalProviderImpl(
          service,
          base::MakeRefCounted<ExtensionMigrator>(
              profile, extension_misc::kGoogleDriveAppId,
              extension_misc::kDocsOfflineExtensionId),
          profile, ManifestLocation::kExternalPref,
          ManifestLocation::kExternalPrefDownload,
          Extension::FROM_WEBSTORE | Extension::WAS_INSTALLED_BY_DEFAULT));
  drive_migration_provider->set_auto_acknowledge(true);
  provider_list->push_back(std::move(drive_migration_provider));

  provider_list->push_back(std::make_unique<ExternalProviderImpl>(
      service, base::MakeRefCounted<ExternalComponentLoader>(profile), profile,
      ManifestLocation::kInvalidLocation, ManifestLocation::kExternalComponent,
      Extension::FROM_WEBSTORE | Extension::WAS_INSTALLED_BY_DEFAULT));

#if BUILDFLAG(ENABLE_EXTENSIONS)
  if (base::FeatureList::IsEnabled(features::kInitialExternalExtensions)) {
    auto initial_external_extensions_provider =
        std::make_unique<ExternalProviderImpl>(
            service,
            base::MakeRefCounted<InitialExternalExtensionLoader>(
                *profile->GetPrefs()),
            profile, ManifestLocation::kExternalPref,
            ManifestLocation::kExternalPrefDownload, Extension::FROM_WEBSTORE);
    initial_external_extensions_provider->set_allow_updates(true);
    initial_external_extensions_provider->set_auto_acknowledge(false);
    provider_list->push_back(std::move(initial_external_extensions_provider));
  }
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)
}

}  // namespace extensions
