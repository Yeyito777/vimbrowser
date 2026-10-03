// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/live_caption/live_caption_bubble_settings.h"

#include "components/live_caption/caption_bubble_settings.h"
#include "components/live_caption/pref_names.h"
#include "components/prefs/pref_service.h"

namespace captions {

LiveCaptionBubbleSettings::LiveCaptionBubbleSettings(PrefService* profile_prefs)
    : profile_prefs_(profile_prefs) {}

LiveCaptionBubbleSettings::~LiveCaptionBubbleSettings() = default;

bool LiveCaptionBubbleSettings::GetLiveCaptionBubbleExpanded() {
  return profile_prefs_->GetBoolean(prefs::kLiveCaptionBubbleExpanded);
}

void LiveCaptionBubbleSettings::SetLiveCaptionEnabled(bool enabled) {
  profile_prefs_->SetBoolean(prefs::kLiveCaptionEnabled, enabled);
}

void LiveCaptionBubbleSettings::SetLiveCaptionBubbleExpanded(bool expanded) {
  profile_prefs_->SetBoolean(prefs::kLiveCaptionBubbleExpanded, expanded);
}

bool LiveCaptionBubbleSettings::ShouldAdjustPositionOnExpand() {
  return false;
}

}  // namespace captions
