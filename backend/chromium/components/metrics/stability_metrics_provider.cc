// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/stability_metrics_provider.h"

#include <string>

#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "build/build_config.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/metrics/stability_metrics_helper.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "third_party/metrics_proto/system_profile.pb.h"


namespace metrics {

namespace {


}  // namespace

StabilityMetricsProvider::StabilityMetricsProvider(PrefService* local_state)
    : local_state_(local_state) {}

StabilityMetricsProvider::~StabilityMetricsProvider() = default;

// static
void StabilityMetricsProvider::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterIntegerPref(prefs::kStabilityFileMetricsUnsentFilesCount,
                                0);
  registry->RegisterIntegerPref(prefs::kStabilityFileMetricsUnsentSamplesCount,
                                0);

}

void StabilityMetricsProvider::Init() {
}

void StabilityMetricsProvider::ClearSavedStabilityMetrics() {
  // The 0 is a valid value for the below prefs, clears pref instead
  // of setting to default value.
  local_state_->ClearPref(prefs::kStabilityFileMetricsUnsentFilesCount);
  local_state_->ClearPref(prefs::kStabilityFileMetricsUnsentSamplesCount);

}

void StabilityMetricsProvider::ProvideStabilityMetrics(
    SystemProfileProto* system_profile) {

  if (local_state_->HasPrefPath(prefs::kStabilityFileMetricsUnsentFilesCount)) {
    UMA_STABILITY_HISTOGRAM_COUNTS_100(
        "Stability.Internals.FileMetricsProvider.BrowserMetrics."
        "UnsentFilesCount",
        local_state_->GetInteger(prefs::kStabilityFileMetricsUnsentFilesCount));
    local_state_->ClearPref(prefs::kStabilityFileMetricsUnsentFilesCount);
  }

  if (local_state_->HasPrefPath(
          prefs::kStabilityFileMetricsUnsentSamplesCount)) {
    UMA_STABILITY_HISTOGRAM_CUSTOM_COUNTS(
        "Stability.Internals.FileMetricsProvider.BrowserMetrics."
        "UnsentSamplesCount",
        local_state_->GetInteger(
            prefs::kStabilityFileMetricsUnsentSamplesCount),
        0, 1000000, 50);
    local_state_->ClearPref(prefs::kStabilityFileMetricsUnsentSamplesCount);
  }

}

void StabilityMetricsProvider::LogCrash(base::Time last_live_timestamp) {
  StabilityMetricsHelper::RecordStabilityEvent(
      StabilityEventType::kBrowserCrash);

}

void StabilityMetricsProvider::LogLaunch() {
  StabilityMetricsHelper::RecordStabilityEvent(StabilityEventType::kLaunch);
}


void StabilityMetricsProvider::IncrementPrefValue(const char* path) {
  int value = local_state_->GetInteger(path);
  local_state_->SetInteger(path, value + 1);
}

int StabilityMetricsProvider::GetAndClearPrefValue(const char* path,
                                                   int* value) {
  *value = local_state_->GetInteger(path);
  if (*value != 0)
    local_state_->SetInteger(path, 0);
  return *value;
}

}  // namespace metrics
