// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_LIVE_CAPTION_CAPTION_BUBBLE_SETTINGS_H_
#define COMPONENTS_LIVE_CAPTION_CAPTION_BUBBLE_SETTINGS_H_

namespace captions {

// Caption Bubble Settings allows caption bubble to get and observe the caption
// bubble settings which can be set from chrome settings in case of live caption
// or from school tools UI in case of BabelOrca. It also allows storage and
// retrieval of the settings set by the user from the caption bubble itself.
class CaptionBubbleSettings {
 public:
  CaptionBubbleSettings(const CaptionBubbleSettings&) = delete;
  CaptionBubbleSettings& operator=(const CaptionBubbleSettings&) = delete;

  virtual ~CaptionBubbleSettings() = default;

  virtual bool GetLiveCaptionBubbleExpanded() = 0;

  virtual void SetLiveCaptionEnabled(bool enabled) = 0;
  virtual void SetLiveCaptionBubbleExpanded(bool expanded) = 0;

  virtual bool ShouldAdjustPositionOnExpand() = 0;

 protected:
  CaptionBubbleSettings() = default;
};

}  // namespace captions

#endif  // COMPONENTS_LIVE_CAPTION_CAPTION_BUBBLE_SETTINGS_H_
