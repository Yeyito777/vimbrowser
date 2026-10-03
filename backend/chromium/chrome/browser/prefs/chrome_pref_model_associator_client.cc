// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prefs/chrome_pref_model_associator_client.h"

#include <cstdint>

#include "base/check_is_test.h"
#include "base/memory/singleton.h"
#include "build/build_config.h"
#include "chrome/browser/ui/toolbar/toolbar_pref_names.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/website_settings_info.h"
#include "components/content_settings/core/browser/website_settings_registry.h"


ChromePrefModelAssociatorClient::ChromePrefModelAssociatorClient() = default;

ChromePrefModelAssociatorClient::~ChromePrefModelAssociatorClient() = default;

base::Value ChromePrefModelAssociatorClient::MaybeMergePreferenceValues(
    std::string_view pref_name,
    const base::Value& local_value,
    const base::Value& server_value) const {
  if (pref_name == prefs::kNetworkEasterEggHighScore) {
    // Case: Both values have expected type.
    if (local_value.is_int() && server_value.is_int()) {
      return base::Value(std::max(local_value.GetInt(), server_value.GetInt()));
    }
    // Case: Only one value has expected type.
    if (local_value.is_int()) {
      return base::Value(local_value.GetInt());
    }
    if (server_value.is_int()) {
      return base::Value(server_value.GetInt());
    }
    // Case: Neither value has expected type.
    return base::Value();
  }
  return base::Value();
}

const sync_preferences::SyncablePrefsDatabase&
ChromePrefModelAssociatorClient::GetSyncablePrefsDatabase() const {
  return chrome_syncable_prefs_database_;
}
