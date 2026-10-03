// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/tab_group_sync/feature_utils.h"

#include "build/build_config.h"
#include "components/prefs/pref_service.h"
#include "components/saved_tab_groups/public/features.h"


namespace tab_groups {


bool IsTabGroupSyncEnabled(PrefService* pref_service) {

  return true;
}

}  // namespace tab_groups
