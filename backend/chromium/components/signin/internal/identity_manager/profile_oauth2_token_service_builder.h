// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SIGNIN_INTERNAL_IDENTITY_MANAGER_PROFILE_OAUTH2_TOKEN_SERVICE_BUILDER_H_
#define COMPONENTS_SIGNIN_INTERNAL_IDENTITY_MANAGER_PROFILE_OAUTH2_TOKEN_SERVICE_BUILDER_H_

#include <memory>

#include "build/build_config.h"
#include "build/buildflag.h"
#include "components/signin/public/base/signin_buildflags.h"

#include "base/memory/scoped_refptr.h"


class AccountTrackerService;
class PrefService;
class ProfileOAuth2TokenService;
class SigninClient;


namespace signin {
enum class AccountConsistencyMethod;
}

namespace network {
class NetworkConnectionTracker;
}

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
class TokenWebData;
namespace unexportable_keys {
class UnexportableKeyService;
}
#endif


std::unique_ptr<ProfileOAuth2TokenService> BuildProfileOAuth2TokenService(
    PrefService* pref_service,
    AccountTrackerService* account_tracker_service,
    network::NetworkConnectionTracker* network_connection_tracker,
    signin::AccountConsistencyMethod account_consistency,
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
    bool delete_signin_cookies_on_exit,
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
    scoped_refptr<TokenWebData> token_web_data,
    unexportable_keys::UnexportableKeyService* unexportable_key_service,
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)
    SigninClient* signin_client);
#endif  // COMPONENTS_SIGNIN_INTERNAL_IDENTITY_MANAGER_PROFILE_OAUTH2_TOKEN_SERVICE_BUILDER_H_
