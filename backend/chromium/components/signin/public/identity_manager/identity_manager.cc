// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/signin/public/identity_manager/identity_manager.h"

#include <optional>
#include <string>

#include "base/functional/bind.h"
#include "base/not_fatal_until.h"
#include "base/observer_list.h"
#include "build/build_config.h"
#include "components/signin/internal/identity_manager/account_fetcher_service.h"
#include "components/signin/internal/identity_manager/account_tracker_service.h"
#include "components/signin/internal/identity_manager/gaia_cookie_manager_service.h"
#include "components/signin/public/base/consent_level.h"
#include "components/signin/public/base/signin_buildflags.h"
#include "components/signin/public/base/signin_client.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/signin/public/identity_manager/account_info.h"
#include "components/signin/public/identity_manager/accounts_cookie_mutator.h"
#include "components/signin/public/identity_manager/accounts_in_cookie_jar_info.h"
#include "components/signin/public/identity_manager/accounts_mutator.h"
#include "components/signin/public/identity_manager/device_accounts_synchronizer.h"
#include "components/signin/public/identity_manager/diagnostics_provider.h"
#include "components/signin/public/identity_manager/primary_account_mutator.h"
#include "google_apis/gaia/google_service_auth_error.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"


#if BUILDFLAG(ENABLE_DICE_SUPPORT)
#include "components/signin/internal/identity_manager/mutable_profile_oauth2_token_service_delegate.h"
#endif

namespace signin {

IdentityManager::InitParameters::InitParameters() = default;

IdentityManager::InitParameters::InitParameters(InitParameters&&) = default;

IdentityManager::InitParameters::~InitParameters() = default;

IdentityManager::IdentityManager(IdentityManager::InitParameters&& parameters)
    : account_tracker_service_(std::move(parameters.account_tracker_service)),
      token_service_(std::move(parameters.token_service)),
      gaia_cookie_manager_service_(
          std::move(parameters.gaia_cookie_manager_service)),
      primary_account_manager_(std::move(parameters.primary_account_manager)),
      account_fetcher_service_(std::move(parameters.account_fetcher_service)),
      signin_client_(parameters.signin_client),
      identity_mutator_(std::make_unique<IdentityMutator>(
          std::move(parameters.primary_account_mutator),
          std::move(parameters.accounts_mutator),
          std::move(parameters.accounts_cookie_mutator),
          std::move(parameters.device_accounts_synchronizer))),
      diagnostics_provider_(std::move(parameters.diagnostics_provider)),
      account_consistency_(parameters.account_consistency),
      weak_pointer_factory_(this) {
  DCHECK(account_fetcher_service_);
  DCHECK(diagnostics_provider_);
  DCHECK(signin_client_);

  primary_account_manager_observation_.Observe(primary_account_manager_.get());
  token_service_observation_.Observe(token_service_.get());
  token_service_->AddAccessTokenDiagnosticsObserver(this);

  // IdentityManager owns the ATS, GCMS and PO2TS instances and will outlive
  // them, so base::Unretained is safe.
  account_tracker_service_->SetOnAccountUpdatedCallback(base::BindRepeating(
      &IdentityManager::OnAccountUpdated, base::Unretained(this)));
  account_tracker_service_->SetOnAccountRemovedCallback(base::BindRepeating(
      &IdentityManager::OnAccountRemoved, base::Unretained(this)));
  gaia_cookie_manager_service_->SetGaiaAccountsInCookieUpdatedCallback(
      base::BindRepeating(&IdentityManager::OnGaiaAccountsInCookieUpdated,
                          base::Unretained(this)));
  gaia_cookie_manager_service_->SetGaiaCookieDeletedByUserActionCallback(
      base::BindRepeating(&IdentityManager::OnGaiaCookieDeletedByUserAction,
                          base::Unretained(this)));
  token_service_->SetRefreshTokenAvailableFromSourceCallback(
      base::BindRepeating(&IdentityManager::OnRefreshTokenAvailableFromSource,
                          base::Unretained(this)));
  token_service_->SetRefreshTokenRevokedFromSourceCallback(
      base::BindRepeating(&IdentityManager::OnRefreshTokenRevokedFromSource,
                          base::Unretained(this)));

}

IdentityManager::~IdentityManager() {
}

void IdentityManager::Shutdown() {
  for (auto& observer : observer_list_) {
    observer.OnIdentityManagerShutdown(this);
  }

  // It is no longer safe to use the SigninClient beyond this point, everything
  // depending on it must be destroyed.
  token_service_->RemoveAccessTokenDiagnosticsObserver(this);
  token_service_observation_.Reset();
  primary_account_manager_observation_.Reset();

  diagnostics_provider_.reset();
  identity_mutator_.reset();
  account_fetcher_service_.reset();
  gaia_cookie_manager_service_.reset();
  primary_account_manager_.reset();
  token_service_.reset();
  account_tracker_service_.reset();
}


void IdentityManager::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void IdentityManager::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

// TODO(crbug.com/40584518) change return type to std::optional<CoreAccountInfo>
CoreAccountInfo IdentityManager::GetPrimaryAccountInfo(
    ConsentLevel consent) const {
  return primary_account_manager_->GetPrimaryAccountInfo(consent);
}

CoreAccountId IdentityManager::GetPrimaryAccountId(ConsentLevel consent) const {
  return GetPrimaryAccountInfo(consent).account_id;
}

bool IdentityManager::HasPrimaryAccount(ConsentLevel consent) const {
  return primary_account_manager_->HasPrimaryAccount(consent);
}

std::unique_ptr<AccessTokenFetcher>
IdentityManager::CreateAccessTokenFetcherWithDynamicScopesForAccount(
    const CoreAccountId& account_id,
    OAuthConsumerId oauth_consumer_id,
    const ScopeSet& scopes,
    AccessTokenFetcher::TokenCallback callback,
    AccessTokenFetcher::Mode mode,
    AccessTokenFetcher::Source token_source) {
  signin::OAuthConsumer oauth_consumer =
      signin::GetOAuthConsumerForDynamicScopes(oauth_consumer_id, scopes);
  return std::make_unique<AccessTokenFetcher>(
      account_id, oauth_consumer_id, oauth_consumer, token_service_.get(),
      primary_account_manager_.get(), std::move(callback), mode, token_source);
}

std::unique_ptr<AccessTokenFetcher>
IdentityManager::CreateAccessTokenFetcherForAccount(
    const CoreAccountId& account_id,
    OAuthConsumerId oauth_consumer_id,
    AccessTokenFetcher::TokenCallback callback,
    AccessTokenFetcher::Mode mode,
    AccessTokenFetcher::Source token_source) {
  signin::OAuthConsumer oauth_consumer =
      signin_client_->GetOAuthConsumerFromId(oauth_consumer_id);
  return std::make_unique<AccessTokenFetcher>(
      account_id, oauth_consumer_id, oauth_consumer, token_service_.get(),
      primary_account_manager_.get(), std::move(callback), mode, token_source);
}

std::unique_ptr<AccessTokenFetcher>
IdentityManager::CreateAccessTokenFetcherForAccount(
    const CoreAccountId& account_id,
    OAuthConsumerId oauth_consumer_id,
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
    AccessTokenFetcher::TokenCallback callback,
    AccessTokenFetcher::Mode mode) {
  signin::OAuthConsumer oauth_consumer =
      signin_client_->GetOAuthConsumerFromId(oauth_consumer_id);
  return std::make_unique<AccessTokenFetcher>(
      account_id, oauth_consumer_id, oauth_consumer, token_service_.get(),
      primary_account_manager_.get(), url_loader_factory, std::move(callback),
      mode);
}

void IdentityManager::RemoveAccessTokenFromCache(
    const CoreAccountId& account_id,
    OAuthConsumerId oauth_consumer_id,
    const std::string& access_token) {
  if (account_id.empty() || access_token.empty()) {
    return;
  }

  ScopeSet scopes =
      signin_client_->GetOAuthConsumerFromId(oauth_consumer_id).GetScopes();
  token_service_->InvalidateAccessToken(account_id, scopes, access_token);
}

std::vector<CoreAccountInfo> IdentityManager::GetAccountsWithRefreshTokens()
    const {
  std::vector<CoreAccountId> account_ids_with_tokens =
      token_service_->GetAccounts();

  std::vector<CoreAccountInfo> accounts;
  accounts.reserve(account_ids_with_tokens.size());

  for (const CoreAccountId& account_id : account_ids_with_tokens) {
    accounts.push_back(GetAccountInfoForAccountWithRefreshToken(account_id));
  }

  return accounts;
}

std::vector<AccountInfo>
IdentityManager::GetExtendedAccountInfoForAccountsWithRefreshToken() const {
  std::vector<CoreAccountId> account_ids_with_tokens =
      token_service_->GetAccounts();

  std::vector<AccountInfo> accounts;
  accounts.reserve(account_ids_with_tokens.size());

  for (const CoreAccountId& account_id : account_ids_with_tokens) {
    accounts.push_back(GetAccountInfoForAccountWithRefreshToken(account_id));
  }

  return accounts;
}

bool IdentityManager::HasPrimaryAccountWithRefreshToken(
    ConsentLevel consent_level) const {
  return HasAccountWithRefreshToken(GetPrimaryAccountId(consent_level));
}

bool IdentityManager::HasAccountWithRefreshToken(
    const CoreAccountId& account_id) const {
  return token_service_->RefreshTokenIsAvailable(account_id);
}


bool IdentityManager::AreRefreshTokensLoaded() const {
  return token_service_->AreAllCredentialsLoaded();
}

bool IdentityManager::HasAccountWithRefreshTokenInPersistentErrorState(
    const CoreAccountId& account_id) const {
  return GetErrorStateOfRefreshTokenForAccount(account_id).IsPersistentError();
}

#if BUILDFLAG(ENABLE_DICE_SUPPORT)
bool IdentityManager::HasAccountWithBoundRefreshToken(
    const CoreAccountId& account_id) const {
  return !token_service_->GetWrappedBindingKey(account_id).empty();
}

bool IdentityManager::AllBoundTokensShareSameBindingKey() const {
  return token_service_->AllBoundTokensShareSameBindingKey();
}

std::vector<uint8_t> IdentityManager::GetWrappedBindingKey() const {
  CHECK(AreRefreshTokensLoaded());
  // All bound tokens are supposed to use the same key. Having two different
  // keys should be considered a bug. To be extra safe, we check the primary
  // account first.
  if (HasPrimaryAccount(ConsentLevel::kSignin)) {
    const std::vector<uint8_t> wrapped_binding_key =
        token_service_->GetWrappedBindingKey(
            GetPrimaryAccountId(ConsentLevel::kSignin));
    if (!wrapped_binding_key.empty()) {
      return wrapped_binding_key;
    }
  }
  for (const CoreAccountId& account_id : token_service_->GetAccounts()) {
    const std::vector<uint8_t> wrapped_binding_key =
        token_service_->GetWrappedBindingKey(account_id);
    if (!wrapped_binding_key.empty()) {
      return wrapped_binding_key;
    }
  }
  return {};
}
#endif  // BUILDFLAG(ENABLE_DICE_SUPPORT)

GoogleServiceAuthError IdentityManager::GetErrorStateOfRefreshTokenForAccount(
    const CoreAccountId& account_id) const {
  return token_service_->GetAuthError(account_id);
}

AccountInfo IdentityManager::FindExtendedAccountInfo(
    const CoreAccountInfo& account_info) const {
  return FindExtendedAccountInfoByAccountId(account_info.account_id);
}

AccountInfo IdentityManager::FindExtendedAccountInfoByAccountId(
    const CoreAccountId& account_id) const {
  // Skip the the token check if the switch is enabled, for consistency with the
  // behavior of FindExtendedAccountInfoByEmailAddress
  if (!HasAccountWithRefreshToken(account_id) &&
      !base::FeatureList::IsEnabled(
          switches::kSkipRefreshTokenCheckInIdentityManager)) {
    return AccountInfo();
  }
  // AccountTrackerService returns an empty AccountInfo if the account is not
  // found.
  return account_tracker_service_->GetAccountInfo(account_id);
}

AccountInfo IdentityManager::FindExtendedAccountInfoByEmailAddress(
    const std::string& email_address) const {
  AccountInfo account_info =
      account_tracker_service_->FindAccountInfoByEmail(email_address);
  // Skip the the token check if the switch is enabled.
  // This prevents a crash that occurs when the account info is retrieved before
  // the account's refresh token is available, causing the check to fail.
  // See https://crbug.com/366252188 and https://crbug.com/40183609
  if (base::FeatureList::IsEnabled(
          switches::kSkipRefreshTokenCheckInIdentityManager)) {
    return account_info;
  }
  // AccountTrackerService always returns an AccountInfo, even on failure. In
  // case of failure, the AccountInfo will be unpopulated, thus we should not
  // be able to find a valid refresh token.
  return HasAccountWithRefreshToken(account_info.account_id) ? account_info
                                                             : AccountInfo();
}

AccountInfo IdentityManager::FindExtendedAccountInfoByGaiaId(
    const GaiaId& gaia_id) const {
  AccountInfo account_info =
      account_tracker_service_->FindAccountInfoByGaiaId(gaia_id);
  // Skip the the token check if the switch is enabled, for consistency with the
  // behavior of FindExtendedAccountInfoByEmailAddress
  if (base::FeatureList::IsEnabled(
          switches::kSkipRefreshTokenCheckInIdentityManager)) {
    return account_info;
  }
  // AccountTrackerService always returns an AccountInfo, even on failure. In
  // case of failure, the AccountInfo will be unpopulated, thus we should not
  // be able to find a valid refresh token.
  return HasAccountWithRefreshToken(account_info.account_id) ? account_info
                                                             : AccountInfo();
}

AccountsInCookieJarInfo IdentityManager::GetAccountsInCookieJar() const {
  return gaia_cookie_manager_service_->ListAccounts();
}

PrimaryAccountMutator* IdentityManager::GetPrimaryAccountMutator() {
  return identity_mutator_->GetPrimaryAccountMutator();
}

AccountsMutator* IdentityManager::GetAccountsMutator() {
  return identity_mutator_->GetAccountsMutator();
}

AccountsCookieMutator* IdentityManager::GetAccountsCookieMutator() {
  return identity_mutator_->GetAccountsCookieMutator();
}

DeviceAccountsSynchronizer* IdentityManager::GetDeviceAccountsSynchronizer() {
  return identity_mutator_->GetDeviceAccountsSynchronizer();
}


void IdentityManager::AddDiagnosticsObserver(DiagnosticsObserver* observer) {
  diagnostics_observation_list_.AddObserver(observer);
}

void IdentityManager::RemoveDiagnosticsObserver(DiagnosticsObserver* observer) {
  diagnostics_observation_list_.RemoveObserver(observer);
}

void IdentityManager::OnNetworkInitialized() {
  gaia_cookie_manager_service_->InitCookieListener();
  account_fetcher_service_->OnNetworkInitialized();
}

CoreAccountId IdentityManager::PickAccountIdForAccount(
    const GaiaId& gaia,
    const std::string& email) const {
  return account_tracker_service_->PickAccountIdForAccount(gaia, email);
}

// static
void IdentityManager::RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  PrimaryAccountManager::RegisterPrefs(registry);
}

// static
void IdentityManager::RegisterProfilePrefs(PrefRegistrySimple* registry) {
  ProfileOAuth2TokenService::RegisterProfilePrefs(registry);
  PrimaryAccountManager::RegisterProfilePrefs(registry);
  AccountFetcherService::RegisterPrefs(registry);
  AccountTrackerService::RegisterPrefs(registry);
  GaiaCookieManagerService::RegisterPrefs(registry);
}

DiagnosticsProvider* IdentityManager::GetDiagnosticsProvider() {
  return diagnostics_provider_.get();
}

void IdentityManager::PrepareForAddingNewAccount() {
  account_fetcher_service_->PrepareForFetchingAccountCapabilities();
}


base::WeakPtr<IdentityManager> IdentityManager::GetWeakPtr() {
  return weak_pointer_factory_.GetWeakPtr();
}

AccountInfo IdentityManager::FindExtendedPrimaryAccountInfo(
    ConsentLevel consent_level) {
  CoreAccountId account_id = GetPrimaryAccountId(consent_level);
  return account_tracker_service_->GetAccountInfo(account_id);
}

PrimaryAccountManager* IdentityManager::GetPrimaryAccountManager() const {
  return primary_account_manager_.get();
}

ProfileOAuth2TokenService* IdentityManager::GetTokenService() const {
  return token_service_.get();
}

AccountTrackerService* IdentityManager::GetAccountTrackerService() const {
  return account_tracker_service_.get();
}

AccountFetcherService* IdentityManager::GetAccountFetcherService() const {
  return account_fetcher_service_.get();
}

GaiaCookieManagerService* IdentityManager::GetGaiaCookieManagerService() const {
  return gaia_cookie_manager_service_.get();
}


AccountInfo IdentityManager::GetAccountInfoForAccountWithRefreshToken(
    const CoreAccountId& account_id) const {
  // TODO(crbug.com/41434401): This invariant is not currently possible to
  // enforce on Android due to the underlying relationship between
  // O2TS::GetAccounts(), O2TS::RefreshTokenIsAvailable(), and
  // O2TS::Observer::OnRefreshTokenAvailable().
  DCHECK(HasAccountWithRefreshToken(account_id));

  AccountInfo account_info =
      account_tracker_service_->GetAccountInfo(account_id);
  DCHECK(!account_info.IsEmpty());

  return account_info;
}

void IdentityManager::OnPrimaryAccountChanged(
    const PrimaryAccountChangeEvent& event_details) {
  CoreAccountId event_primary_account_id =
      event_details.GetCurrentState().primary_account.account_id;
  DCHECK_EQ(event_primary_account_id,
            GetPrimaryAccountId(event_details.GetCurrentState().consent_level));
  for (auto& observer : observer_list_) {
    observer.OnPrimaryAccountChanged(event_details);
    // Ensure that |observer| did not change the primary account as otherwise
    // |event_details| would not longer be correct.
    DCHECK_EQ(
        event_primary_account_id,
        GetPrimaryAccountId(event_details.GetCurrentState().consent_level));
  }

}

void IdentityManager::OnRefreshTokenAvailable(const CoreAccountId& account_id) {
  CoreAccountInfo account_info =
      GetAccountInfoForAccountWithRefreshToken(account_id);

  for (auto& observer : observer_list_) {
    observer.OnRefreshTokenUpdatedForAccount(account_info);
  }
}

void IdentityManager::OnRefreshTokenRevoked(const CoreAccountId& account_id) {
  for (auto& observer : observer_list_) {
    observer.OnRefreshTokenRemovedForAccount(account_id);
  }
}

void IdentityManager::OnRefreshTokensLoaded() {
  for (auto& observer : observer_list_) {
    observer.OnRefreshTokensLoaded();
  }
}

void IdentityManager::OnEndBatchChanges() {
  for (auto& observer : observer_list_) {
    observer.OnEndBatchOfRefreshTokenStateChanges();
  }
}

void IdentityManager::OnAuthErrorChanged(
    const CoreAccountId& account_id,
    const GoogleServiceAuthError& auth_error,
    signin_metrics::SourceForRefreshTokenOperation token_operation_source) {
  CoreAccountInfo account_info =
      GetAccountInfoForAccountWithRefreshToken(account_id);

  for (auto& observer : observer_list_) {
    observer.OnErrorStateOfRefreshTokenUpdatedForAccount(
        account_info, auth_error, token_operation_source);
  }
}


void IdentityManager::OnGaiaAccountsInCookieUpdated(
    const AccountsInCookieJarInfo& accounts_in_cookie_jar_info,
    const GoogleServiceAuthError& error) {
  bool succeeded = error == GoogleServiceAuthError::AuthErrorNone();
  CHECK(accounts_in_cookie_jar_info.AreAccountsFresh() == succeeded);

  for (auto& observer : observer_list_) {
    observer.OnAccountsInCookieUpdated(accounts_in_cookie_jar_info, error);
  }
}

void IdentityManager::OnGaiaCookieDeletedByUserAction() {
  for (auto& observer : observer_list_) {
    observer.OnAccountsCookieDeletedByUserAction();
  }
}

void IdentityManager::OnAccessTokenRequested(const CoreAccountId& account_id,
                                             const std::string& consumer_id,
                                             const ScopeSet& scopes) {
  for (auto& observer : diagnostics_observation_list_) {
    observer.OnAccessTokenRequested(account_id, consumer_id, scopes);
  }
}

void IdentityManager::OnFetchAccessTokenComplete(
    const CoreAccountId& account_id,
    const std::string& consumer_id,
    const ScopeSet& scopes,
    const GoogleServiceAuthError& error,
    base::Time expiration_time) {
  for (auto& observer : diagnostics_observation_list_) {
    observer.OnAccessTokenRequestCompleted(account_id, consumer_id, scopes,
                                           error, expiration_time);
  }
}

void IdentityManager::OnAccessTokenRemoved(const CoreAccountId& account_id,
                                           const ScopeSet& scopes) {
  for (auto& observer : diagnostics_observation_list_) {
    observer.OnAccessTokenRemovedFromCache(account_id, scopes);
  }
}

void IdentityManager::OnRefreshTokenAvailableFromSource(
    const CoreAccountId& account_id,
    bool is_refresh_token_valid,
    const std::string& source) {
  for (auto& observer : diagnostics_observation_list_) {
    observer.OnRefreshTokenUpdatedForAccountFromSource(
        account_id, is_refresh_token_valid, source);
  }
}

void IdentityManager::OnRefreshTokenRevokedFromSource(
    const CoreAccountId& account_id,
    const std::string& source) {
  // Copy the account ID to avoid a use-after-free if one of the observers
  // owns the reference to the account ID and destroys it in
  // `OnRefreshTokenRemovedForAccountFromSource()`.
  CoreAccountId account_id_copy = account_id;
  for (auto& observer : diagnostics_observation_list_) {
    observer.OnRefreshTokenRemovedForAccountFromSource(account_id_copy, source);
  }
}

void IdentityManager::OnAccountUpdated(const AccountInfo& info) {
  if (HasPrimaryAccount(signin::ConsentLevel::kSignin)) {
    const CoreAccountId primary_account_id =
        GetPrimaryAccountId(ConsentLevel::kSignin);
    if (primary_account_id == info.account_id) {
      primary_account_manager_->UpdatePrimaryAccountInfo();
    }
  }

  for (auto& observer : observer_list_) {
    observer.OnExtendedAccountInfoUpdated(info);
  }
}

void IdentityManager::OnAccountRemoved(const AccountInfo& info) {
  for (auto& observer : observer_list_) {
    observer.OnExtendedAccountInfoRemoved(info);
  }
}

}  // namespace signin
