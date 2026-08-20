// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/fonts/font_unique_name_lookup.h"

#include "build/build_config.h"

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
#include "third_party/blink/renderer/platform/fonts/linux/font_unique_name_lookup_linux.h"
#endif

namespace blink {

FontUniqueNameLookup::FontUniqueNameLookup() = default;

// static
std::unique_ptr<FontUniqueNameLookup>
FontUniqueNameLookup::GetPlatformUniqueNameLookup() {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  return std::make_unique<FontUniqueNameLookupLinux>();
#else
  return nullptr;
#endif
}

}  // namespace blink
