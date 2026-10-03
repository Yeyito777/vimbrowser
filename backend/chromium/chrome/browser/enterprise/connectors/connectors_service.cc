// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/connectors/connectors_service.h"

#include <memory>
#include <variant>

#include "base/check_op.h"
#include "base/memory/singleton.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/enterprise/browser_management/management_identity.h"
#include "chrome/browser/enterprise/connectors/connectors_manager.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "chrome/browser/policy/dm_token_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/reporting_util.h"
#include "chrome/browser/safe_browsing/cloud_content_scanning/deep_scanning_utils.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "components/enterprise/browser/controller/browser_dm_token_storage.h"
#include "components/enterprise/buildflags/buildflags.h"
#include "components/enterprise/common/proto/connectors.pb.h"
#include "components/enterprise/connectors/core/connectors_manager_base.h"
#include "components/enterprise/connectors/core/connectors_prefs.h"
#include "components/enterprise/connectors/core/service_provider_config.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/policy/core/common/cloud/cloud_policy_store.h"
#include "components/policy/core/common/cloud/dm_token.h"
#include "components/policy/core/common/cloud/machine_level_user_cloud_policy_manager.h"
#include "components/policy/core/common/cloud/machine_level_user_cloud_policy_store.h"
#include "components/policy/core/common/policy_types.h"
#include "components/safe_browsing/buildflags.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/user_prefs/user_prefs.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_context.h"
#include "content/public/common/url_constants.h"
#include "device_management_backend.pb.h"
#include "google_apis/gaia/gaia_auth_util.h"

#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
#include "chrome/browser/extensions/chrome_content_browser_client_extensions_part.h"
#include "extensions/browser/extension_registry_factory.h"
#endif

#include "components/policy/core/common/cloud/profile_cloud_policy_manager.h"

#if BUILDFLAG(SAFE_BROWSING_AVAILABLE)
#include "chrome/browser/enterprise/connectors/common.h"
#endif

namespace enterprise_connectors {

namespace {

std::string GetClientId(Profile* profile) {
  std::string client_id;
  client_id = policy::BrowserDMTokenStorage::Get()->RetrieveClientId();
  return client_id;
}


bool IsManagedGuestSession() {
  return false;
}
}  // namespace

// --------------------------------
// ConnectorsService implementation
// --------------------------------

ConnectorsService::ConnectorsService(
    content::BrowserContext* context,
    std::unique_ptr<ConnectorsManagerBase> manager)
    : ConnectorsServiceBase(std::move(manager)), context_(context) {
  DCHECK(context_);
}

ConnectorsService::~ConnectorsService() = default;

std::unique_ptr<ClientMetadata> ConnectorsService::GetBasicClientMetadata(
    Profile* profile) {
  auto metadata = std::make_unique<ClientMetadata>();
  // We need to return profile and browser DM tokens, even in cases where the
  // reporting policy is disabled, in order to support merging rules.
  std::optional<std::string> browser_dm_token = GetBrowserDmToken();
  if (browser_dm_token.has_value()) {
    metadata->mutable_device()->set_dm_token(*browser_dm_token);
  }

  std::optional<std::string> profile_dm_token =
      reporting::GetUserDmToken(profile);
  if (profile_dm_token.has_value()) {
    metadata->mutable_profile()->set_dm_token(*profile_dm_token);
  }

  // In this case, we are just using the client metadata to indicate to
  // WebProtect whether or not the request is coming from a Managed Guest
  // Session on ChromeOS.
  metadata->set_is_chrome_os_managed_guest_session(IsManagedGuestSession());
  return metadata;
}

std::optional<ReportingSettings> ConnectorsService::GetReportingSettings() {

  return ConnectorsServiceBase::GetReportingSettings();
}


std::string ConnectorsService::GetManagementDomain() {
  if (!ConnectorsEnabled()) {
    return std::string();
  }

  std::optional<policy::PolicyScope> scope = std::nullopt;
  for (const char* scope_pref :
       {enterprise_connectors::kEnterpriseRealTimeUrlCheckScope,
        AnalysisConnectorScopePref(AnalysisConnector::FILE_ATTACHED),
        AnalysisConnectorScopePref(AnalysisConnector::FILE_DOWNLOADED),
        AnalysisConnectorScopePref(AnalysisConnector::BULK_DATA_ENTRY),
        AnalysisConnectorScopePref(AnalysisConnector::PRINT),
        kOnSecurityEventScopePref}) {
    std::optional<DmToken> dm_token = GetDmToken(scope_pref);
    if (dm_token.has_value()) {
      scope = dm_token.value().scope;

      // Having one CBCM Connector policy set implies that profile ones will be
      // ignored for another domain, so the loop can stop immediately.
      if (scope == policy::PolicyScope::POLICY_SCOPE_MACHINE) {
        break;
      }
    }
  }

  if (!scope.has_value()) {
    return std::string();
  }

  if (scope.value() == policy::PolicyScope::POLICY_SCOPE_USER) {
    return GetAccountManagerIdentity(Profile::FromBrowserContext(context_))
        .value_or(std::string());
  }

  policy::MachineLevelUserCloudPolicyManager* manager =
      g_browser_process->browser_policy_connector()
          ->machine_level_user_cloud_policy_manager();
  if (!manager) {
    return std::string();
  }

  policy::CloudPolicyStore* store = manager->store();
  return (store && store->has_policy())
             ? gaia::ExtractDomainName(store->policy()->username())
             : std::string();
}

std::string ConnectorsService::GetRealTimeUrlCheckIdentifier() const {
  auto dm_token = GetDmToken(kEnterpriseRealTimeUrlCheckScope);
  if (!dm_token) {
    return std::string();
  }

  Profile* profile = Profile::FromBrowserContext(context_);
  if (dm_token->scope == policy::POLICY_SCOPE_MACHINE) {
    return GetClientId(profile);
  }

#if BUILDFLAG(SAFE_BROWSING_AVAILABLE)
  auto* identity_manager = IdentityManagerFactory::GetForProfile(profile);
  if (!identity_manager) {
    return std::string();
  }

  return GetProfileEmail(identity_manager);
#else
  return std::string();
#endif
}

std::optional<ConnectorsService::DmToken> ConnectorsService::GetDmToken(
    const char* scope_pref) const {
  auto browser_dm_token = GetBrowserDmToken();
  policy::PolicyScope scope = GetPolicyScope(scope_pref);
  std::string token_string = scope == policy::POLICY_SCOPE_USER
                                 ? GetProfileDmToken().value_or("")
                                 : browser_dm_token.value_or("");
  if (token_string.empty()) {
    return std::nullopt;
  }
  return DmToken(token_string, scope);
}

std::optional<std::string> ConnectorsService::GetBrowserDmToken() const {
  policy::DMToken dm_token =
      policy::GetDMToken(Profile::FromBrowserContext(context_));

  if (!dm_token.is_valid()) {
    return std::nullopt;
  }

  return dm_token.value();
}

policy::PolicyScope ConnectorsService::GetPolicyScope(
    const char* scope_pref) const {
  return ConnectorsServiceBase::GetPolicyScope(scope_pref);
}

bool ConnectorsService::ConnectorsEnabled() const {
  Profile* profile = Profile::FromBrowserContext(context_);

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  // On desktop, the guest profile is actually the primary OTR profile of
  // the "regular" guest profile.  The regular guest profile is never used
  // directly by users.  Also, user are not able to create child OTR profiles
  // from guest profiles, the menu item "New incognito window" is not
  // available.  So, if this is a guest session, allow it only if it is a
  // child OTR profile as well.
  if (profile->IsGuestSession()) {
    return profile->GetOriginalProfile() != profile;
  }

  // Never allow system profiles.
  if (profile->IsSystemProfile()) {
    return false;
  }
#endif

  return !profile->IsOffTheRecord() || profile->IsGuestSession();
}

PrefService* ConnectorsService::GetPrefs() {
  return Profile::FromBrowserContext(context_)->GetPrefs();
}

const PrefService* ConnectorsService::GetPrefs() const {
  return Profile::FromBrowserContext(context_)->GetPrefs();
}

policy::CloudPolicyManager*
ConnectorsService::GetManagedUserCloudPolicyManager() const {
  return Profile::FromBrowserContext(context_)->GetCloudPolicyManager();
}

std::unique_ptr<ClientMetadata> ConnectorsService::BuildClientMetadata(
    bool is_cloud) {
  auto reporting_settings = GetReportingSettings();

  Profile* profile = Profile::FromBrowserContext(context_);
  if (is_cloud && !reporting_settings.has_value()) {
    return GetBasicClientMetadata(profile);
  }

  auto metadata = std::make_unique<ClientMetadata>(
      reporting::GetContextAsClientMetadata(profile));

  // Device info is only useful for cloud service providers since local
  // providers can already determine all this info themselves. For this reason,
  // we only include browser metadata.
  if (!is_cloud) {
    PopulateBrowserMetadata(/*include_device_info=*/true,
                            metadata->mutable_browser());
    return metadata;
  }

  metadata->set_is_chrome_os_managed_guest_session(IsManagedGuestSession());

#if BUILDFLAG(SAFE_BROWSING_AVAILABLE)
  bool include_device_info =
      IncludeDeviceInfo(profile, reporting_settings.value().per_profile);

  PopulateBrowserMetadata(include_device_info, metadata->mutable_browser());

  if (include_device_info) {
    PopulateDeviceMetadata(GetClientId(profile), metadata->mutable_device());
  }
#endif

  return metadata;
}

bool ConnectorsService::IsURLExemptFromAnalysis(const GURL& url,
                                                AnalysisConnector connector) {

  return ConnectorsServiceBase::IsURLExemptFromAnalysis(url, connector);
}

// ---------------------------------------
// ConnectorsServiceFactory implementation
// ---------------------------------------

// static
ConnectorsServiceFactory* ConnectorsServiceFactory::GetInstance() {
  return base::Singleton<ConnectorsServiceFactory>::get();
}

ConnectorsService* ConnectorsServiceFactory::GetForBrowserContext(
    content::BrowserContext* context) {
  return static_cast<ConnectorsService*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

ConnectorsServiceFactory::ConnectorsServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "ConnectorsService",
          BrowserContextDependencyManager::GetInstance()) {
#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
  DependsOn(extensions::ExtensionRegistryFactory::GetInstance());
#endif
}

ConnectorsServiceFactory::~ConnectorsServiceFactory() = default;

std::unique_ptr<KeyedService>
ConnectorsServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<ConnectorsService>(
      context,
      std::make_unique<ConnectorsManager>(user_prefs::UserPrefs::Get(context),
                                          GetServiceProviderConfig()));
}

content::BrowserContext* ConnectorsServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
#if BUILDFLAG(ENTERPRISE_CONTENT_ANALYSIS)
  // Do not construct the connectors service if the extensions are disabled for
  // the given context.
  if (extensions::ChromeContentBrowserClientExtensionsPart::
          AreExtensionsDisabledForProfile(context)) {
    return nullptr;
  }
#endif

  // On Chrome OS, settings from the primary/main profile apply to all
  // profiles, besides incognito.
  // However, the primary/main profile might not exist in tests - then the
  // provided |context| is still used.
  if (context && !context->IsOffTheRecord() &&
      !Profile::FromBrowserContext(context)->AsTestingProfile() &&
      !context->ShutdownStarted()) {
  }
  return context;
}

}  // namespace enterprise_connectors
