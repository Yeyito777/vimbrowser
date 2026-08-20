// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/chrome_policy_conversions_client.h"

#include <set>
#include <string>
#include <utility>

#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_process_platform_part.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "chrome/browser/policy/profile_policy_connector.h"
#include "chrome/browser/policy/schema_registry_service.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "components/policy/core/browser/configuration_policy_handler_list.h"
#include "components/policy/core/browser/policy_conversions.h"
#include "components/policy/core/browser/policy_conversions_client.h"
#include "components/policy/core/browser/policy_error_map.h"
#include "components/policy/core/common/policy_logger.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/core/common/policy_namespace.h"
#include "components/policy/core/common/policy_service.h"
#include "extensions/buildflags/buildflags.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#include "extensions/common/manifest.h"
#include "extensions/common/manifest_constants.h"
#endif


namespace policy {

namespace {


}  // namespace

ChromePolicyConversionsClient::ChromePolicyConversionsClient(
    content::BrowserContext* context) {
  DCHECK(context);
  profile_ = Profile::FromBrowserContext(
      GetBrowserContextRedirectedInIncognito(context));
}

ChromePolicyConversionsClient::~ChromePolicyConversionsClient() = default;

PolicyService* ChromePolicyConversionsClient::GetPolicyService() const {
  return profile_->GetProfilePolicyConnector()->policy_service();
}

SchemaRegistry* ChromePolicyConversionsClient::GetPolicySchemaRegistry() const {
  auto* schema_registry_service = profile_->GetPolicySchemaRegistryService();
  if (schema_registry_service) {
    return schema_registry_service->registry();
  }
  return nullptr;
}

const ConfigurationPolicyHandlerList*
ChromePolicyConversionsClient::GetHandlerList() const {
  return g_browser_process->browser_policy_connector()->GetHandlerList();
}

bool ChromePolicyConversionsClient::HasUserPolicies() const {
  return profile_ != nullptr;
}

base::ListValue ChromePolicyConversionsClient::GetExtensionPolicies(
    PolicyDomain policy_domain) {
  base::ListValue policies;

#if BUILDFLAG(ENABLE_EXTENSIONS)

  const bool for_signin_screen =
      policy_domain == POLICY_DOMAIN_SIGNIN_EXTENSIONS;
  Profile* extension_profile = profile_;

  const extensions::ExtensionRegistry* registry =
      extensions::ExtensionRegistry::Get(extension_profile);
  if (!registry) {
    LOG_POLICY(ERROR, POLICY_PROCESSING)
        << "Cannot dump extension policies, no extension registry";
    return policies;
  }
  auto* schema_registry_service =
      extension_profile->GetOriginalProfile()->GetPolicySchemaRegistryService();
  if (!schema_registry_service || !schema_registry_service->registry()) {
    LOG_POLICY(ERROR, POLICY_PROCESSING)
        << "Cannot dump extension policies, no schema registry service";
    return policies;
  }
  const scoped_refptr<SchemaMap> schema_map =
      schema_registry_service->registry()->schema_map();
  const extensions::ExtensionSet extension_set =
      registry->GenerateInstalledExtensionsSet();
  for (const auto& extension : extension_set) {
    // Skip this extension if it's not an enterprise extension.
    if (!extension->manifest()->FindPath(
            extensions::manifest_keys::kStorageManagedSchema)) {
      continue;
    }

    PolicyNamespace policy_namespace =
        PolicyNamespace(policy_domain, extension->id());
    PolicyErrorMap empty_error_map;
    base::DictValue extension_policies =
        GetPolicyValues(extension_profile->GetProfilePolicyConnector()
                            ->policy_service()
                            ->GetPolicies(policy_namespace),
                        &empty_error_map, PoliciesSet(), PoliciesSet(),
                        GetKnownPolicies(schema_map, policy_namespace));
    base::DictValue extension_policies_data;
    extension_policies_data.Set(policy::kNameKey, extension->name());
    extension_policies_data.Set(policy::kIdKey, extension->id());
    extension_policies_data.Set("forSigninScreen", for_signin_screen);
    extension_policies_data.Set("isExtension", true);
    extension_policies_data.Set(policy::kPoliciesKey,
                                std::move(extension_policies));
    policies.Append(std::move(extension_policies_data));
  }
#endif
  return policies;
}


}  // namespace policy
