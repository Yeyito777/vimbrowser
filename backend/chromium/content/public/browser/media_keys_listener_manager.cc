// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/media_keys_listener_manager.h"

#include "build/build_config.h"

#include "base/feature_list.h"
#include "media/base/media_switches.h"

namespace content {

// static
bool MediaKeysListenerManager::IsMediaKeysListenerManagerEnabled() {
  return base::FeatureList::IsEnabled(media::kHardwareMediaKeyHandling);
}

MediaKeysListenerManager::~MediaKeysListenerManager() = default;

}  // namespace content
