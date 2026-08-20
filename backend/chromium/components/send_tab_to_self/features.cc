// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/send_tab_to_self/features.h"

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/time/time.h"
#include "build/build_config.h"


namespace send_tab_to_self {

BASE_FEATURE(kSendTabToSelfEnableNotificationTimeOut,
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kSendTabToSelfPropagateFormFields,
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kSendTabToSelfPropagateScrollPosition,
             base::FEATURE_DISABLED_BY_DEFAULT);


}  // namespace send_tab_to_self
