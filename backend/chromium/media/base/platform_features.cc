// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/platform_features.h"

#include "base/feature_list.h"
#include "build/build_config.h"
#include "media/base/media_switches.h"

namespace media {

bool IsVp9kSVCHWDecodingEnabled() {
  return false;
}

}  // namespace media
