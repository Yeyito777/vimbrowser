// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "partition_alloc/address_space_randomization.h"

#include "partition_alloc/build_config.h"
#include "partition_alloc/buildflags.h"
#include "partition_alloc/partition_alloc_check.h"
#include "partition_alloc/random.h"


namespace partition_alloc {

uintptr_t GetRandomPageBase() {
  uintptr_t random = static_cast<uintptr_t>(internal::RandomValue());

#if PA_BUILDFLAG(HAS_64_BIT_POINTERS)
  random <<= 32ULL;
  random |= static_cast<uintptr_t>(internal::RandomValue());

  // The ASLRMask() and ASLROffset() constants will be suitable for the
  // OS and build configuration.
  random &= internal::ASLRMask();
  random += internal::ASLROffset();
#else  // PA_BUILDFLAG(HAS_64_BIT_POINTERS)
  random &= internal::ASLRMask();
  random += internal::ASLROffset();
#endif  // PA_BUILDFLAG(HAS_64_BIT_POINTERS)

  PA_DCHECK(!(random & internal::PageAllocationGranularityOffsetMask()));
  return random;
}

}  // namespace partition_alloc
