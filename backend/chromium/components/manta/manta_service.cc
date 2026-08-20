// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/manta/manta_service.h"

#include <memory>

#include "base/containers/fixed_flat_set.h"
#include "base/memory/scoped_refptr.h"
#include "components/account_id/account_id.h"
#include "components/manta/anchovy/anchovy_provider.h"
#include "components/manta/provider_params.h"
#include "components/signin/public/identity_manager/account_capabilities.h"
#include "components/signin/public/identity_manager/identity_manager.h"
#include "components/signin/public/identity_manager/tribool.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"


namespace manta {

namespace {

FeatureSupportStatus ConvertToMantaFeatureSupportStatus(signin::Tribool value) {
  switch (value) {
    case signin::Tribool::kUnknown:
      return FeatureSupportStatus::kUnknown;
    case signin::Tribool::kTrue:
      return FeatureSupportStatus::kSupported;
    case signin::Tribool::kFalse:
      return FeatureSupportStatus::kUnsupported;
  }
}

}  // namespace

MantaService::MantaService(
    scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory,
    signin::IdentityManager* identity_manager,
    bool is_demo_mode,
    bool is_otr_profile,
    const std::string& chrome_version,
    const version_info::Channel chrome_channel,
    const std::string& locale)
    : shared_url_loader_factory_(shared_url_loader_factory),
      identity_manager_(identity_manager),
      is_demo_mode_(is_demo_mode),
      is_otr_profile_(is_otr_profile),
      chrome_version_(chrome_version),
      chrome_channel_(chrome_channel),
      locale_(locale) {}

MantaService::~MantaService() = default;

FeatureSupportStatus MantaService::SupportsOrca() {
  if (is_demo_mode_) {
    return FeatureSupportStatus::kSupported;
  }
  return CanAccessMantaFeaturesWithoutMinorRestrictions();
}

FeatureSupportStatus
MantaService::CanAccessMantaFeaturesWithoutMinorRestrictions() {
  if (identity_manager_ == nullptr) {
    return FeatureSupportStatus::kUnknown;
  }

  const auto account_id =
      identity_manager_->GetPrimaryAccountId(signin::ConsentLevel::kSignin);

  if (account_id.empty()) {
    return FeatureSupportStatus::kUnknown;
  }

  const AccountInfo extended_account_info =
      identity_manager_->FindExtendedAccountInfoByAccountId(account_id);

  // Fetches and uses the shared account capability for manta service.
  return ConvertToMantaFeatureSupportStatus(
      extended_account_info.capabilities.can_use_manta_service());
}

std::unique_ptr<AnchovyProvider> MantaService::CreateAnchovyProvider() {
  // Anchovy Provider supports API Key Requests for OTR profiles and doesn't
  // requires a valid identity_manager.
  const ProviderParams provider_params = {/*use_api_key=*/is_otr_profile_,
                                          chrome_version_, chrome_channel_,
                                          locale_};

  return std::make_unique<AnchovyProvider>(shared_url_loader_factory_,
                                           identity_manager_, provider_params);
}


void MantaService::Shutdown() {
  identity_manager_ = nullptr;
}

}  // namespace manta
