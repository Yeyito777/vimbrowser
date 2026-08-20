// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_PROCESS_PROCESS_INFO_H_
#define BASE_PROCESS_PROCESS_INFO_H_

#include "base/base_export.h"
#include "base/process/process_handle.h"
#include "build/build_config.h"

namespace base {


#if BUILDFLAG(IS_MAC)
// Checks if the responsible process has Bluetooth metadata in its Info.plist
// file. See https://bugs.chromium.org/p/chromium/issues/detail?id=945969 and
// https://bugs.chromium.org/p/chromium/issues/detail?id=996993.
BASE_EXPORT bool DoesResponsibleProcessHaveBluetoothMetadata();
#endif

}  // namespace base

#endif  // BASE_PROCESS_PROCESS_INFO_H_
