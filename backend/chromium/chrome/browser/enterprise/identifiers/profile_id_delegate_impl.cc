// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/identifiers/profile_id_delegate_impl.h"

#include <utility>

#include "base/check.h"
#include "base/uuid.h"
#include "build/buildflag.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/enterprise/browser/identifiers/identifiers_prefs.h"
#include "components/prefs/pref_service.h"

#include "components/enterprise/browser/controller/browser_dm_token_storage.h"


namespace enterprise {

namespace {

const void* const kPresetProfileManagementData = &kPresetProfileManagementData;

// Creates and persists the profile GUID if one does not already exist
void CreateProfileGUID(Profile* profile, const base::FilePath& profile_path) {
  auto* prefs = profile->GetPrefs();
  if (!prefs->GetString(kProfileGUIDPref).empty()) {
    return;
  }

  auto* preset_profile_management_data =
      PresetProfileManagementData::Get(profile);
  std::string profile_guid = preset_profile_management_data->guid();
  if (profile_guid.empty()) {
    profile_guid = base::Uuid::GenerateRandomV4().AsLowercaseString();
  }

  prefs->SetString(kProfileGUIDPref, std::move(profile_guid));
  preset_profile_management_data->ClearGuid();
}

}  // namespace

PresetProfileManagementData* PresetProfileManagementData::Get(
    Profile* profile) {
  CHECK(profile);

  if (!profile->GetUserData(kPresetProfileManagementData)) {
    profile->SetUserData(
        kPresetProfileManagementData,
        std::make_unique<PresetProfileManagementData>(std::string()));
  }

  return static_cast<PresetProfileManagementData*>(
      profile->GetUserData(kPresetProfileManagementData));
}

void PresetProfileManagementData::SetGuid(std::string guid) {
  CHECK(!guid.empty());
  CHECK(guid_.empty());

  guid_ = std::move(guid);
}

void PresetProfileManagementData::ClearGuid() {
  guid_.clear();
}

PresetProfileManagementData::PresetProfileManagementData(
    std::string preset_guid)
    : guid_(std::move(preset_guid)) {}

PresetProfileManagementData::~PresetProfileManagementData() = default;

ProfileIdDelegateImpl::ProfileIdDelegateImpl(Profile* profile)
    : profile_(profile) {
  CHECK(profile_);
  CreateProfileGUID(profile_, profile->GetPath());
}

ProfileIdDelegateImpl::~ProfileIdDelegateImpl() = default;

std::string ProfileIdDelegateImpl::GetDeviceId() {
  return ProfileIdDelegateImpl::GetId();
}

// static
std::string ProfileIdDelegateImpl::GetId() {
  // Gets the device ID from the BrowserDMTokenStorage.
  std::string device_id =
      policy::BrowserDMTokenStorage::Get()->RetrieveClientId();


  return device_id;
}

}  // namespace enterprise
