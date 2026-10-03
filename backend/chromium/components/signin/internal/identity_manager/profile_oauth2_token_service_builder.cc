// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/signin/internal/identity_manager/profile_oauth2_token_service_builder.h"

#include <string>
#include <utility>

#include "build/build_config.h"
#include "components/prefs/pref_service.h"
#include "components/signin/internal/identity_manager/profile_oauth2_token_service.h"
#include "components/signin/public/base/account_consistency_method.h"
#include "components/signin/public/base/device_id_helper.h"
#include "components/signin/public/base/signin_client.h"
#include "components/signin/public/base/signin_switches.h"


#if BUILDFLAG(ENABLE_DICE_SUPPORT)
#include "components/signin/internal/identity_manager/mutable_profile_oauth2_token_service_delegate.h"
#include "components/signin/internal/identity_manager/token_binding_helper.h"
#include "components/signin/public/webdata/token_web_data.h"
#include "components/unexportable_keys/unexportable_key_service.h"
#endif



namespace {

#if BUILDFLAG(ENABLE_DICE_SUPPORT)

std::unique_ptr<MutableProfileOAuth2TokenServiceDelegate>
CreateMutableProfileOAuthDelegate(
    AccountTrackerService* account_tracker_service,
    signin::AccountConsistencyMethod account_consistency,
    bool delete_signin_cookies_on_exit,
    scoped_refptr<TokenWebData> token_web_data,
    SigninClient* signin_client,
    unexportable_keys::UnexportableKeyService* unexportable_key_service,
    network::NetworkConnectionTracker* network_connection_tracker) {
  // When signin cookies are cleared on exit and Dice is enabled, all tokens
  // should also be cleared.
  RevokeAllTokensOnLoad revoke_all_tokens_on_load =
      (account_consistency == signin::AccountConsistencyMethod::kDice) &&
              delete_signin_cookies_on_exit
          ? RevokeAllTokensOnLoad::kDeleteSiteDataOnExit
          : RevokeAllTokensOnLoad::kNo;

  std::unique_ptr<TokenBindingHelper> token_binding_helper;
  if (unexportable_key_service &&
      switches::IsChromeRefreshTokenBindingEnabled(signin_client->GetPrefs())) {
    token_binding_helper =
        std::make_unique<TokenBindingHelper>(*unexportable_key_service);
  }

  return std::make_unique<MutableProfileOAuth2TokenServiceDelegate>(
      signin_client, account_tracker_service, network_connection_tracker,
      token_web_data, account_consistency, revoke_all_tokens_on_load,
      std::move(token_binding_helper),
      MutableProfileOAuth2TokenServiceDelegate::FixRequestErrorCallback()
  );
}
#endif  // BUILDFLAG(IS_ANDROID)

std::unique_ptr<ProfileOAuth2TokenServiceDelegate>
CreateOAuth2TokenServiceDelegate(
    AccountTrackerService* account_tracker_service,
    signin::AccountConsistencyMethod account_consistency,
    SigninClient* signin_client,
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
    bool delete_signin_cookies_on_exit,
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
    scoped_refptr<TokenWebData> token_web_data,
    unexportable_keys::UnexportableKeyService* unexportable_key_service,
#endif
    network::NetworkConnectionTracker* network_connection_tracker) {
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
  // Fall back to |MutableProfileOAuth2TokenServiceDelegate| on all platforms
  // other than Android, iOS, and Chrome OS (Ash).
  return CreateMutableProfileOAuthDelegate(
      account_tracker_service, account_consistency,
      delete_signin_cookies_on_exit, token_web_data, signin_client,
      unexportable_key_service,
      network_connection_tracker);
#else
  NOTREACHED();
#endif  // BUILDFLAG(IS_ANDROID)
}

}  // namespace

std::unique_ptr<ProfileOAuth2TokenService> BuildProfileOAuth2TokenService(
    PrefService* pref_service,
    AccountTrackerService* account_tracker_service,
    network::NetworkConnectionTracker* network_connection_tracker,
    signin::AccountConsistencyMethod account_consistency,
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
    bool delete_signin_cookies_on_exit,
    scoped_refptr<TokenWebData> token_web_data,
    unexportable_keys::UnexportableKeyService* unexportable_key_service,
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)
    SigninClient* signin_client) {
// On ChromeOS the device ID is not managed by the token service.
  // Ensure the device ID is not empty. This is important for Dice, because the
  // device ID is needed on the network thread, but can only be generated on the
  // main thread.
  std::string device_id = signin::GetSigninScopedDeviceId(pref_service);
  DCHECK(!device_id.empty());

  return std::make_unique<ProfileOAuth2TokenService>(
      pref_service,
      CreateOAuth2TokenServiceDelegate(
          account_tracker_service, account_consistency, signin_client,
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
          delete_signin_cookies_on_exit,
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
          token_web_data, unexportable_key_service,
#endif
          network_connection_tracker));
}
