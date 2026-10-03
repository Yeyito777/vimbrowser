// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "data_saver.h"

#include <optional>

#include "base/feature_list.h"
#include "base/metrics/histogram_functions.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"


namespace {
std::optional<bool> g_override_data_saver_for_testing;

}  // namespace

namespace data_saver {

void OverrideIsDataSaverEnabledForTesting(bool flag) {
  g_override_data_saver_for_testing = flag;
}

void ResetIsDataSaverEnabledForTesting() {
  g_override_data_saver_for_testing = std::nullopt;
}

void FetchDataSaverOSSettingAsynchronously() {
}

bool IsDataSaverEnabled() {
  if (g_override_data_saver_for_testing.has_value()) {
    return g_override_data_saver_for_testing.value();
  }
  return false;
}

}  // namespace data_saver
