// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/signin/public/identity_manager/identity_mutator.h"

#include "build/build_config.h"
#include "components/signin/public/base/consent_level.h"
#include "components/signin/public/identity_manager/accounts_cookie_mutator.h"
#include "components/signin/public/identity_manager/accounts_mutator.h"
#include "components/signin/public/identity_manager/device_accounts_synchronizer.h"
#include "components/signin/public/identity_manager/primary_account_mutator.h"


namespace signin {


IdentityMutator::IdentityMutator(
    std::unique_ptr<PrimaryAccountMutator> primary_account_mutator,
    std::unique_ptr<AccountsMutator> accounts_mutator,
    std::unique_ptr<AccountsCookieMutator> accounts_cookie_mutator,
    std::unique_ptr<DeviceAccountsSynchronizer> device_accounts_synchronizer)
    : primary_account_mutator_(std::move(primary_account_mutator)),
      accounts_mutator_(std::move(accounts_mutator)),
      accounts_cookie_mutator_(std::move(accounts_cookie_mutator)),
      device_accounts_synchronizer_(std::move(device_accounts_synchronizer)) {
  DCHECK(accounts_cookie_mutator_);
  DCHECK(!accounts_mutator_ || !device_accounts_synchronizer_)
      << "Cannot have both an AccountsMutator and a DeviceAccountsSynchronizer";

}

IdentityMutator::~IdentityMutator() {
}


PrimaryAccountMutator* IdentityMutator::GetPrimaryAccountMutator() {
  return primary_account_mutator_.get();
}

AccountsMutator* IdentityMutator::GetAccountsMutator() {
  return accounts_mutator_.get();
}

AccountsCookieMutator* IdentityMutator::GetAccountsCookieMutator() {
  return accounts_cookie_mutator_.get();
}

DeviceAccountsSynchronizer* IdentityMutator::GetDeviceAccountsSynchronizer() {
  return device_accounts_synchronizer_.get();
}
}  // namespace signin
