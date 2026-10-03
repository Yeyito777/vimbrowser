// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PREFS_BROWSER_PREFS_H_
#define CHROME_BROWSER_PREFS_BROWSER_PREFS_H_

#include <string>
#include <string_view>

#include "build/build_config.h"

class PrefRegistrySimple;
class PrefService;

namespace base {
class FilePath;
}

namespace user_prefs {
class PrefRegistrySyncable;
}

// Gets the current country from the browser process. May be empty if the
// browser process cannot supply the country.
std::string GetCountry();

// Register all prefs that will be used via the local state PrefService.
void RegisterLocalState(PrefRegistrySimple* registry);

void RegisterScreenshotPrefs(PrefRegistrySimple* registry);

void RegisterGeminiSettingsPrefs(user_prefs::PrefRegistrySyncable* registry);

// Register all prefs that will be used via a PrefService attached to a user
// Profile using the locale of |g_browser_process|.
void RegisterUserProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

// Register all prefs that will be used via a PrefService attached to a user
// Profile with the given |locale|.
void RegisterUserProfilePrefs(user_prefs::PrefRegistrySyncable* registry,
                              const std::string& locale);


// Migrate/cleanup deprecated prefs in |local_state|. Over time, long deprecated
// prefs should be removed as new ones are added, but this call should never go
// away (even if it becomes an empty call for some time) as it should remain
// *the* place to drop deprecated browser-level (Local State) prefs at.
void MigrateObsoleteLocalStatePrefs(PrefService* local_state);

// Migrate/cleanup deprecated prefs in |profile_prefs|. Over time, long
// deprecated prefs should be removed as new ones are added, but this call
// should never go away (even if it becomes an empty call for some time) as it
// should remain *the* place to drop deprecated profile prefs at.
void MigrateObsoleteProfilePrefs(PrefService* profile_prefs,
                                 const base::FilePath& profile_path);

#endif  // CHROME_BROWSER_PREFS_BROWSER_PREFS_H_
