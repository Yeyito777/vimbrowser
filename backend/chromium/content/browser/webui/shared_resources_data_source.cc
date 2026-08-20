// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/webui/shared_resources_data_source.h"

#include <set>

#include "content/public/browser/web_ui_data_source.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/webui/resources/grit/webui_resources.h"
#include "ui/webui/resources/grit/webui_resources_map.h"


namespace content {

namespace {


}  // namespace

void PopulateSharedResourcesDataSource(WebUIDataSource* source) {
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::WorkerSrc, "worker-src blob: 'self';");

  // Note: Don't put generated Mojo bindings here. Mojo bindings should be
  // included in the either the ts_library() target for the UI using them (if
  // they are only used by one UI) or in //ui/webui/resources/mojo:build_ts
  // (if used by multiple UIs).
  source->AddResourcePaths(kWebuiResources);
}

}  // namespace content
