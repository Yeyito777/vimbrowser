// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/live_caption/live_caption_bubble_settings.h"

#include "base/values.h"
#include "components/live_caption/pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace captions {
namespace {

class LiveCaptionBubbleSettingsTest : public testing::Test {
 protected:
  void SetUp() override {
    pref_service_.registry()->RegisterBooleanPref(
        prefs::kLiveCaptionBubbleExpanded, false);
    pref_service_.registry()->RegisterBooleanPref(prefs::kLiveCaptionEnabled,
                                                  false);
  }

  TestingPrefServiceSimple pref_service_;
};

TEST_F(LiveCaptionBubbleSettingsTest, SetLiveCaptionBubbleExpanded) {
  LiveCaptionBubbleSettings caption_bubble_settings(&pref_service_);
  caption_bubble_settings.SetLiveCaptionBubbleExpanded(true);

  EXPECT_TRUE(pref_service_.GetBoolean(prefs::kLiveCaptionBubbleExpanded));
}

TEST_F(LiveCaptionBubbleSettingsTest, SetLiveCaptionEnabled) {
  LiveCaptionBubbleSettings caption_bubble_settings(&pref_service_);
  caption_bubble_settings.SetLiveCaptionEnabled(true);

  EXPECT_TRUE(pref_service_.GetBoolean(prefs::kLiveCaptionEnabled));
}

TEST_F(LiveCaptionBubbleSettingsTest, GetLiveCaptionBubbleExpanded) {
  LiveCaptionBubbleSettings caption_bubble_settings(&pref_service_);
  pref_service_.SetUserPref(prefs::kLiveCaptionBubbleExpanded,
                            base::Value(true));

  EXPECT_TRUE(caption_bubble_settings.GetLiveCaptionBubbleExpanded());
}

}  // namespace
}  // namespace captions
