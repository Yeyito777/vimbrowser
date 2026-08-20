// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTING_BACKEND_PRINTING_RESTRICTIONS_H_
#define PRINTING_BACKEND_PRINTING_RESTRICTIONS_H_

#include "base/component_export.h"
#include "build/build_config.h"


namespace printing {


// Allowed background graphics modes.
// This is used in pref file and should never change.
enum class BackgroundGraphicsModeRestriction {
  kUnset = 0,
  kEnabled = 1,
  kDisabled = 2,
};

// Dictionary keys to be used with `kPrintingPaperSizeDefault` policy.
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kPaperSizeName[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kPaperSizeNameCustomOption[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kPaperSizeCustomSize[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kPaperSizeWidth[];
COMPONENT_EXPORT(PRINT_BACKEND) extern const char kPaperSizeHeight[];

}  // namespace printing

#endif  // PRINTING_BACKEND_PRINTING_RESTRICTIONS_H_
