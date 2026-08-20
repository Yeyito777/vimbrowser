// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura/native_window_occlusion_tracker.h"

#include "base/metrics/field_trial_params.h"
#include "build/build_config.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/ui_base_features.h"


namespace aura {

// static
void NativeWindowOcclusionTracker::EnableNativeWindowOcclusionTracking(
    WindowTreeHost* host) {
}

// static
void NativeWindowOcclusionTracker::DisableNativeWindowOcclusionTracking(
    WindowTreeHost* host) {
}

// static
bool NativeWindowOcclusionTracker::IsNativeWindowOcclusionTrackingAlwaysEnabled(
    WindowTreeHost* host) {
  return false;
}

}  // namespace aura
