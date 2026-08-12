// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/back_forward_transition_animation_manager.h"

#include "base/auto_reset.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/common/content_client.h"

namespace content {

// static
bool BackForwardTransitionAnimationManager::
    ShouldAnimateBackForwardTransitions() {
  return content::GetContentClient()
      ->browser()
      ->ShouldAnimateBackForwardTransitions();
}

// static
base::AutoReset<int>
BackForwardTransitionAnimationManager::SetMinRequiredPhysicalRamMbForTesting(
    int /*mb*/) {
  return base::AutoReset<int>(nullptr, 0);
}

// static
bool BackForwardTransitionAnimationManager::ShouldAnimateNavigationTransition(
    NavigationDirection /*navigation_direction*/,
    ui::BackGestureEventSwipeEdge /*edge*/) {
  return false;
}
}  // namespace content
