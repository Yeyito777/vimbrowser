// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_TEST_TEST_SYNC_USER_SETTINGS_H_
#define COMPONENTS_SYNC_TEST_TEST_SYNC_USER_SETTINGS_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "components/signin/public/base/gaia_id_hash.h"
#include "components/sync/base/data_type.h"
#include "components/sync/base/user_selectable_type.h"
#include "components/sync/service/sync_user_settings.h"

namespace syncer {

class TestSyncService;

// Test implementation of SyncUserSettings that mostly forwards calls to a
// TestSyncService.
class TestSyncUserSettings : public SyncUserSettings {
 public:
  explicit TestSyncUserSettings(TestSyncService* service);
  ~TestSyncUserSettings() override;

  // SyncUserSettings implementation.
  bool IsInitialSyncFeatureSetupComplete() const override;

  void SetInitialSyncFeatureSetupComplete(
      SyncFirstSetupCompleteSource source) override;

  bool IsSyncEverythingEnabled() const override;
  UserSelectableTypeSet GetSelectedTypes() const override;
  bool IsTypeManagedByPolicy(UserSelectableType type) const override;
  bool IsTypeManagedByCustodian(UserSelectableType type) const override;
  SyncUserSettings::UserSelectableTypePrefState GetTypePrefStateForAccount(
      UserSelectableType type) const override;
  void SetSelectedTypes(bool sync_everything,
                        UserSelectableTypeSet types) override;
  void SetSelectedType(UserSelectableType type, bool is_type_on) override;
  void ResetSelectedType(UserSelectableType type) override;
  void KeepAccountSettingsPrefsOnlyForUsers(
      const std::vector<GaiaId>& available_gaia_ids) override;
  DataTypeSet GetPreferredDataTypes() const;
  UserSelectableTypeSet GetRegisteredSelectableTypes() const override;


  bool IsCustomPassphraseAllowed() const override;
  bool IsEncryptEverythingEnabled() const override;

  syncer::DataTypeSet GetAllEncryptedDataTypes() const override;
  bool IsPassphraseRequired() const override;
  bool IsPassphraseRequiredForPreferredDataTypes() const override;
  bool IsPassphrasePromptMutedForCurrentProductVersion() const override;
  void MarkPassphrasePromptMutedForCurrentProductVersion() override;
  bool IsTrustedVaultKeyRequired() const override;
  bool IsTrustedVaultKeyRequiredForPreferredDataTypes() const override;
  bool IsTrustedVaultRecoverabilityDegraded() const override;
  bool IsUsingExplicitPassphrase() const override;
  base::Time GetExplicitPassphraseTime() const override;
  std::optional<PassphraseType> GetPassphraseType() const override;

  void SetEncryptionPassphrase(const std::string& passphrase) override;
  bool SetDecryptionPassphrase(const std::string& passphrase) override;

  void SetRegisteredSelectableTypes(UserSelectableTypeSet types);
  void SetInitialSyncFeatureSetupComplete();
  void ClearInitialSyncFeatureSetupComplete();
  void SetTypeIsManagedByPolicy(UserSelectableType type, bool managed);
  void SetTypeIsManagedByCustodian(UserSelectableType type, bool managed);
  void SetCustomPassphraseAllowed(bool allowed);
  void SetPassphraseRequired();
  void SetPassphraseRequired(const std::string& required_passphrase);
  void SetTrustedVaultKeyRequired(bool required);
  void SetTrustedVaultRecoverabilityDegraded(bool degraded);
  void SetIsUsingExplicitPassphrase(bool enabled);
  void SetPassphraseType(PassphraseType type);
  void SetExplicitPassphraseTime(base::Time t);

  void SetDisabledType(UserSelectableType type);


  const std::string& GetEncryptionPassphrase() const;

 private:
  bool IsEncryptedDatatypePreferred() const;

  const raw_ptr<TestSyncService> service_;

  UserSelectableTypeSet registered_selectable_types_ =
      UserSelectableTypeSet::All();
  UserSelectableTypeSet selected_types_ = UserSelectableTypeSet::All();
  UserSelectableTypeSet managed_by_policy_types_;
  UserSelectableTypeSet managed_by_custodian_types_;

  // This can be populated through `SetDisabledType()`. Types are removed from
  // this set once they are enabled again.
  UserSelectableTypeSet disabled_types_;

  bool initial_sync_feature_setup_complete_ = true;
  bool sync_everything_enabled_ = true;

  bool custom_passphrase_allowed_ = true;
  bool passphrase_required_ = false;
  bool trusted_vault_key_required_ = false;
  bool trusted_vault_recoverability_degraded_ = false;
  PassphraseType passphrase_type_ = PassphraseType::kKeystorePassphrase;
  base::Time explicit_passphrase_time_;
  std::string encryption_passphrase_;

};

}  // namespace syncer

#endif  // COMPONENTS_SYNC_TEST_TEST_SYNC_USER_SETTINGS_H_
