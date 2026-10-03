// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/base/features.h"

#include "build/build_config.h"

namespace device {


namespace features {

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)
// Controls whether Web Bluetooth should support confirm-only and confirm-PIN
// pairing mode on Win/Linux
BASE_FEATURE(kWebBluetoothConfirmPairingSupport,
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX)




}  // namespace features
}  // namespace device
