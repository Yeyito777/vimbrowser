// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/component_updater/component_updater_utils.h"

#include "build/build_config.h"

namespace component_updater {

bool IsPerUserInstall() {
  return true;
}

}  // namespace component_updater
