// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/renderer/render_frame_media_playback_options.h"

#include "base/feature_list.h"
#include "build/build_config.h"
#include "content/public/common/content_features.h"


namespace content {
bool IsBackgroundMediaSuspendEnabled() {
  // For non-Android devices, always allow background media to play
  return false;
}
}  // namespace content
