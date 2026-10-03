// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/component_extensions_allowlist/allowlist.h"

#include <stddef.h>

#include "base/containers/fixed_flat_set.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/common/buildflags.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/grit/browser_resources.h"
#include "extensions/buildflags/buildflags.h"
#include "extensions/common/constants.h"
#include "printing/buildflags/buildflags.h"


static_assert(BUILDFLAG(ENABLE_EXTENSIONS_CORE));

namespace extensions {

bool IsComponentExtensionAllowlisted(const std::string& extension_id) {
  constexpr auto kAllowed = base::MakeFixedFlatSet<std::string_view>({
      extension_misc::kInAppPaymentsSupportAppId,
      extension_misc::kPdfExtensionId,
      extension_misc::kReadingModeGDocsHelperExtensionId,
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)
      extension_misc::kTTSEngineExtensionId,
      extension_misc::kComponentUpdaterTTSEngineExtensionId,
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)
  });

  if (kAllowed.contains(extension_id)) {
    return true;
  }

  LOG(ERROR) << "Component extension with id " << extension_id << " not in "
             << "allowlist and is not being loaded as a result.";
  NOTREACHED() << "Component extension with id " << extension_id << " not in "
               << "allowlist and is not being loaded as a result.";
}

bool IsComponentExtensionAllowlisted(int manifest_resource_id) {
  switch (manifest_resource_id) {
    // Please keep the list in alphabetical order.
#if BUILDFLAG(ENABLE_HANGOUT_SERVICES_EXTENSION)
    case IDR_HANGOUT_SERVICES_MANIFEST_V2:
    case IDR_HANGOUT_SERVICES_MANIFEST_V3:
#endif
    case IDR_NETWORK_SPEECH_SYNTHESIS_MANIFEST:
    case IDR_NETWORK_SPEECH_SYNTHESIS_MANIFEST_MV3:
    case IDR_READING_MODE_GDOCS_HELPER_MANIFEST:
    case IDR_WEBSTORE_MANIFEST:

      return true;
  }

  LOG(ERROR) << "Component extension with manifest resource id "
             << manifest_resource_id << " not in allowlist and is not being "
             << "loaded as a result.";
  NOTREACHED() << "Component extension with manifest resource id "
               << manifest_resource_id << " not in allowlist and is not being "
               << "loaded as a result.";
}


}  // namespace extensions
