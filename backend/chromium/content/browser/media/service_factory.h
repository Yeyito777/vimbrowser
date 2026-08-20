// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_MEDIA_SERVICE_FACTORY_H_
#define CONTENT_BROWSER_MEDIA_SERVICE_FACTORY_H_

#include "base/token.h"
#include "build/build_config.h"
#include "content/public/common/cdm_info.h"
#include "media/mojo/mojom/cdm_service.mojom-forward.h"
#include "url/gurl.h"


namespace content {

class BrowserContext;

// Gets an instance of the CdmService for the `browser_context`, `site`, and
// `cdm_info`. Instances are started lazily as needed. The CDM located at
// `cdm_info` is loaded in the sandboxed process to be used by the service.
media::mojom::CdmService& GetCdmService(BrowserContext* browser_context,
                                        const GURL& site,
                                        const CdmInfo& cdm_info);


}  // namespace content

#endif  // CONTENT_BROWSER_MEDIA_SERVICE_FACTORY_H_
