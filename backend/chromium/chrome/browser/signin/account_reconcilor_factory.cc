// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/account_reconcilor_factory.h"

#include <memory>
#include <utility>

#include "base/check.h"
#include "base/notreached.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/account_consistency_mode_manager.h"
#include "chrome/browser/signin/chrome_signin_client_factory.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/signin/core/browser/account_reconcilor.h"
#include "components/signin/core/browser/account_reconcilor_delegate.h"
#include "components/signin/core/browser/mirror_account_reconcilor_delegate.h"
#include "components/signin/public/base/account_consistency_method.h"
#include "components/signin/public/base/signin_buildflags.h"
#include "components/signin/public/base/signin_client.h"


#if BUILDFLAG(ENABLE_DICE_SUPPORT)
#include "components/signin/core/browser/dice_account_reconcilor_delegate.h"
#endif

namespace {


}  // namespace

AccountReconcilorFactory::AccountReconcilorFactory()
    : ProfileKeyedServiceFactory(
          "AccountReconcilor",
          ProfileSelections::Builder()
              .WithRegular(ProfileSelection::kOriginalOnly)
              .WithGuest(ProfileSelection::kNone)
              .Build()) {
  DependsOn(ChromeSigninClientFactory::GetInstance());
  DependsOn(IdentityManagerFactory::GetInstance());
}

AccountReconcilorFactory::~AccountReconcilorFactory() = default;

// static
AccountReconcilor* AccountReconcilorFactory::GetForProfile(Profile* profile) {
  return static_cast<AccountReconcilor*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

// static
AccountReconcilorFactory* AccountReconcilorFactory::GetInstance() {
  static base::NoDestructor<AccountReconcilorFactory> instance;
  return instance.get();
}

std::unique_ptr<KeyedService>
AccountReconcilorFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  signin::IdentityManager* identity_manager =
      IdentityManagerFactory::GetForProfile(profile);
  SigninClient* signin_client =
      ChromeSigninClientFactory::GetForProfile(profile);
  std::unique_ptr<AccountReconcilor> reconcilor =
      std::make_unique<AccountReconcilor>(
          identity_manager, signin_client,
          CreateAccountReconcilorDelegate(profile));
  reconcilor->Initialize(true /* start_reconcile_if_tokens_available */);
  return reconcilor;
}

// static
std::unique_ptr<signin::AccountReconcilorDelegate>
AccountReconcilorFactory::CreateAccountReconcilorDelegate(Profile* profile) {
  signin::AccountConsistencyMethod account_consistency =
      AccountConsistencyModeManager::GetMethodForProfile(profile);
  switch (account_consistency) {
    case signin::AccountConsistencyMethod::kMirror:
      return std::make_unique<signin::MirrorAccountReconcilorDelegate>(
          IdentityManagerFactory::GetForProfile(profile));

    case signin::AccountConsistencyMethod::kDisabled:
      return std::make_unique<signin::AccountReconcilorDelegate>();

    case signin::AccountConsistencyMethod::kDice:
#if BUILDFLAG(ENABLE_DICE_SUPPORT)
      return std::make_unique<signin::DiceAccountReconcilorDelegate>(
          IdentityManagerFactory::GetForProfile(profile));
#else
      NOTREACHED();
#endif
  }

  NOTREACHED();
}
