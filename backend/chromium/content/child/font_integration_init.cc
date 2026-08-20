// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/child/font_integration_init.h"

#include "base/command_line.h"
#include "build/build_config.h"
#include "content/child/font_data/font_data_manager.h"
#include "content/common/features.h"
#include "content/public/common/content_switches.h"

namespace content {

void InitializeFontIntegration() {
  bool is_single_process = base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kSingleProcess);

  // On all platforms that support font data service, don't create it in single
  // process mode because there's already a process-local font manager.
  if (!is_single_process && features::IsFontDataServiceEnabled()) {
    font_data_service::FontDataManager::CreateAndInitialize();
  }
}

void UninitializeFontIntegration() {
}

}  // namespace content
