// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/time/time.h"
#include "build/build_config.h"


#ifndef CHROME_BROWSER_PERFORMANCE_MANAGER_POLICIES_POLICY_FEATURES_H_
#define CHROME_BROWSER_PERFORMANCE_MANAGER_POLICIES_POLICY_FEATURES_H_

namespace performance_manager {
namespace features {



BASE_DECLARE_FEATURE(kSustainedPMUrgentDiscarding);

BASE_DECLARE_FEATURE_PARAM(int,
                           kSustainedPMUrgentDiscarding_PercentAvailableMemory);
BASE_DECLARE_FEATURE_PARAM(base::TimeDelta,
                           kSustainedPMUrgentDiscarding_CheckPressureDelay);
BASE_DECLARE_FEATURE_PARAM(base::TimeDelta,
                           kSustainedPMUrgentDiscarding_SustainedPressureDelay);

}  // namespace features
}  // namespace performance_manager

#endif  // CHROME_BROWSER_PERFORMANCE_MANAGER_POLICIES_POLICY_FEATURES_H_
