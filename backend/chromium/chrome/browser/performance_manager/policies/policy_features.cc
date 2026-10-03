// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/performance_manager/policies/policy_features.h"

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/time/time.h"
#include "build/build_config.h"


namespace performance_manager {
namespace features {



BASE_FEATURE(kSustainedPMUrgentDiscarding, base::FEATURE_DISABLED_BY_DEFAULT);

// The percentage of available memory threshold under which it is considered
// memory pressure.
BASE_FEATURE_PARAM(int,
                   kSustainedPMUrgentDiscarding_PercentAvailableMemory,
                   &kSustainedPMUrgentDiscarding,
                   "percent_available_memory",
                   15);
// Delay between checking the memory pressure state.
BASE_FEATURE_PARAM(base::TimeDelta,
                   kSustainedPMUrgentDiscarding_CheckPressureDelay,
                   &kSustainedPMUrgentDiscarding,
                   "delay_for_check_pressure",
                   base::Seconds(5));
// Delay until the memory pressure state is considered "sustained".
BASE_FEATURE_PARAM(base::TimeDelta,
                   kSustainedPMUrgentDiscarding_SustainedPressureDelay,
                   &kSustainedPMUrgentDiscarding,
                   "delay_for_sustained_pressure",
                   base::Seconds(10));

}  // namespace features
}  // namespace performance_manager
