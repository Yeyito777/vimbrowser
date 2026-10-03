// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/eye_dropper/features.h"

#include "build/build_config.h"

namespace eye_dropper::features {

// Enables the use of WGC for the Eye Dropper screen capture.
BASE_FEATURE(kAllowEyeDropperWGCScreenCapture,
             base::FEATURE_DISABLED_BY_DEFAULT
);

}  // namespace eye_dropper::features
