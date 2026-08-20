// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SETTINGS_PROFILE_INFO_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SETTINGS_PROFILE_INFO_HANDLER_H_

#include <memory>

#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"
#include "components/prefs/pref_change_registrar.h"

#include "chrome/browser/profiles/profile_statistics_common.h"

class Profile;

namespace settings {

class ProfileInfoHandler : public SettingsPageUIHandler,
                           public ProfileAttributesStorage::Observer {
 public:
  static const char kProfileInfoChangedEventName[];
  static const char kProfileStatsCountReadyEventName[];

  explicit ProfileInfoHandler(Profile* profile);

  ProfileInfoHandler(const ProfileInfoHandler&) = delete;
  ProfileInfoHandler& operator=(const ProfileInfoHandler&) = delete;

  ~ProfileInfoHandler() override;

  // SettingsPageUIHandler implementation.
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;


  // ProfileAttributesStorage::Observer implementation.
  void OnProfileNameChanged(const base::FilePath& profile_path,
                            const std::u16string& old_profile_name) override;
  void OnProfileAvatarChanged(const base::FilePath& profile_path) override;

 private:
  FRIEND_TEST_ALL_PREFIXES(ProfileInfoHandlerTest, GetProfileInfo);
  FRIEND_TEST_ALL_PREFIXES(ProfileInfoHandlerTest, PushProfileInfo);

  // Callbacks from the page.
  void HandleGetProfileInfo(const base::ListValue& args);
  void PushProfileInfo();

  void HandleGetProfileStats(const base::ListValue& args);

  // Returns the sum of the counts of individual profile states. Returns 0 if
  // there exists a stat that was not successfully retrieved.
  void PushProfileStatsCount(profiles::ProfileCategoryStats stats);

  base::DictValue GetAccountNameAndIcon();

  // Weak pointer.
  raw_ptr<Profile> profile_;


  base::ScopedObservation<ProfileAttributesStorage,
                          ProfileAttributesStorage::Observer>
      profile_observation_{this};

  // Used to cancel callbacks when JavaScript becomes disallowed.
  base::WeakPtrFactory<ProfileInfoHandler> callback_weak_ptr_factory_{this};
};

}  // namespace settings

#endif  // CHROME_BROWSER_UI_WEBUI_SETTINGS_PROFILE_INFO_HANDLER_H_
