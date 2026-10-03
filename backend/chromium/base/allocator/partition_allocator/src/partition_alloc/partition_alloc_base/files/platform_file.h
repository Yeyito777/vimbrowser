// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PARTITION_ALLOC_PARTITION_ALLOC_BASE_FILES_PLATFORM_FILE_H_
#define PARTITION_ALLOC_PARTITION_ALLOC_BASE_FILES_PLATFORM_FILE_H_

#include "partition_alloc/build_config.h"


// This file defines platform-independent types for dealing with
// platform-dependent files. If possible, use the higher-level base::File class
// rather than these primitives.

namespace partition_alloc::internal::base {

#if PA_BUILDFLAG(IS_POSIX) || PA_BUILDFLAG(IS_FUCHSIA)

using PlatformFile = int;

inline constexpr PlatformFile kInvalidPlatformFile = -1;

#endif

}  // namespace partition_alloc::internal::base

#endif  // PARTITION_ALLOC_PARTITION_ALLOC_BASE_FILES_PLATFORM_FILE_H_
