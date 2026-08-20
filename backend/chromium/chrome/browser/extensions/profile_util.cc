// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/profile_util.h"

#include "build/chromeos_buildflags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "extensions/buildflags/buildflags.h"


static_assert(BUILDFLAG(ENABLE_EXTENSIONS_CORE));

namespace extensions::profile_util {

bool ProfileCanUseNonComponentExtensions(const Profile* profile) {
  if (!profile) {
    return false;
  }
  return profile->IsRegularProfile();
}

Profile* GetLastUsedProfile() {
  return ProfileManager::GetLastUsedProfile();
}

size_t GetNumberOfProfiles() {
  ProfileManager* const manager = GetProfileManager();
  return !manager ? 0 : manager->GetNumberOfProfiles();
}

ProfileManager* GetProfileManager() {
  return g_browser_process->profile_manager();
}


}  // namespace extensions::profile_util
