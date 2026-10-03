// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/external_component_loader.h"

#include "base/memory/scoped_refptr.h"
#include "base/values.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/component_extensions_allowlist/allowlist.h"
#include "chrome/common/buildflags.h"
#include "chrome/common/extensions/extension_constants.h"
#include "content/public/browser/browser_context.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/forced_extensions/install_stage_tracker.h"
#include "extensions/buildflags/buildflags.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/feature_switch.h"
#include "extensions/common/manifest.h"


static_assert(BUILDFLAG(ENABLE_EXTENSIONS_CORE));

namespace extensions {

ExternalComponentLoader::ExternalComponentLoader(Profile* profile)
    : profile_(profile) {}

ExternalComponentLoader::~ExternalComponentLoader() = default;

void ExternalComponentLoader::StartLoading() {
  auto prefs = base::DictValue();
  // Skip in-app payments app on Android. crbug.com/409396604
#if BUILDFLAG(GOOGLE_CHROME_BRANDING) && !BUILDFLAG(IS_ANDROID)
  AddExternalExtension(extension_misc::kInAppPaymentsSupportAppId, prefs);
#endif  // BUILDFLAG(GOOGLE_CHROME_BRANDING) && !BUILDFLAG(IS_ANDROID)


  LoadFinished(std::move(prefs));
}

void ExternalComponentLoader::AddExternalExtension(
    const std::string& extension_id,
    base::DictValue& prefs) {
  if (!IsComponentExtensionAllowlisted(extension_id))
    return;

  prefs.SetByDottedPath(extension_id + ".external_update_url",
                        extension_urls::GetWebstoreUpdateUrl().spec());
}

}  // namespace extensions
