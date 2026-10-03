// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_EMOJI_EMOJI_PANEL_HELPER_H_
#define UI_BASE_EMOJI_EMOJI_PANEL_HELPER_H_

#include <string>

#include "base/component_export.h"
#include "base/functional/callback_forward.h"
#include "build/build_config.h"

namespace ui {

// Returns whether showing the Emoji Panel is supported on this version of
// the operating system.
COMPONENT_EXPORT(UI_BASE_EMOJI) bool IsEmojiPanelSupported();

// Invokes the commands to show the Emoji Panel.
COMPONENT_EXPORT(UI_BASE_EMOJI) void ShowEmojiPanel();


}  // namespace ui

#endif  // UI_BASE_EMOJI_EMOJI_PANEL_HELPER_H_
