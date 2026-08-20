// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/core/embedder/features.h"

#include "build/build_config.h"

namespace mojo {
namespace core {

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
BASE_FEATURE(kMojoUseEventFd, base::FEATURE_DISABLED_BY_DEFAULT);
const base::FeatureParam<int> kMojoUseEventFdPages{&kMojoUseEventFd,
                                                   "MojoUseEventFdPages", 4};
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) ||
        // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(MOJO_SUPPORT_LEGACY_CORE)
BASE_FEATURE(kMojoIpcz, base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(MOJO_SUPPORT_LEGACY_CORE)

BASE_FEATURE(kMojoIpczMemV2, base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kMojoFixGeometricBufferGrowth, base::FEATURE_DISABLED_BY_DEFAULT);


}  // namespace core
}  // namespace mojo
