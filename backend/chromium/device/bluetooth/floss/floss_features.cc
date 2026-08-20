// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/bluetooth/floss/floss_features.h"

#include "base/system/sys_info.h"

namespace floss {
namespace features {


bool IsFlossEnabled() {
  return false;
}

bool IsLLPrivacyAvailable() {
  return false;
}
}  // namespace features
}  // namespace floss
