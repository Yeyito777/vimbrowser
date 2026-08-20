// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/browsing_data/core/pref_names.h"

#include "base/metrics/histogram_macros.h"
#include "base/values.h"
#include "components/browsing_data/core/browsing_data_utils.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"


namespace browsing_data::prefs {

void RegisterBrowserUserPrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(kBrowsingDataLifetime);
  registry->RegisterBooleanPref(kClearBrowsingDataOnExitDeletionPending, false);
  registry->RegisterListPref(kClearBrowsingDataOnExitList);
  // TODO(crbug.com/471197613): When MaybeMigrateToQuickDeletePrefValues is
  // removed, set default value in iOS for the `kDeleteTimePeriod` pref to 15
  // minutes.
  registry->RegisterIntegerPref(
      kDeleteTimePeriod,
      static_cast<int>(browsing_data::TimePeriod::LAST_HOUR));
  registry->RegisterIntegerPref(
      kDeleteTimePeriodBasic,
      static_cast<int>(browsing_data::TimePeriod::LAST_HOUR));
  registry->RegisterBooleanPref(kDeleteBrowsingHistory, true);
  registry->RegisterBooleanPref(kDeleteBrowsingHistoryBasic, true);
  registry->RegisterBooleanPref(kDeleteCache, true);
  registry->RegisterBooleanPref(kDeleteCacheBasic, true);
  registry->RegisterBooleanPref(kDeleteCookies, true);
  registry->RegisterBooleanPref(kDeleteCookiesBasic, true);
  registry->RegisterBooleanPref(kDeletePasswords, false);
  registry->RegisterBooleanPref(kDeleteFormData, false);
  registry->RegisterIntegerPref(
      kClearBrowsingDataHistoryNoticeShownTimes, 0);

  registry->RegisterBooleanPref(kDeleteDownloadHistory, true);
  registry->RegisterBooleanPref(kDeleteHostedAppsData, false);
  registry->RegisterBooleanPref(kDeleteSiteSettings, false);



  registry->RegisterIntegerPref(kLastClearBrowsingDataTab, 0);
  registry->RegisterBooleanPref(kQuickDeleteEverUsed, false);
}


}  // namespace browsing_data::prefs
