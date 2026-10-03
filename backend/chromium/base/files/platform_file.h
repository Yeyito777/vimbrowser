// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FILES_PLATFORM_FILE_H_
#define BASE_FILES_PLATFORM_FILE_H_

#include "base/files/scoped_file.h"
#include "build/build_config.h"


// This file defines platform-independent types for dealing with
// platform-dependent files. If possible, use the higher-level base::File class
// rather than these primitives.

namespace base {

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)

using PlatformFile = int;
using ScopedPlatformFile = ::base::ScopedFD;

constexpr PlatformFile kInvalidPlatformFile = -1;

#endif

}  // namespace base

#endif  // BASE_FILES_PLATFORM_FILE_H_
