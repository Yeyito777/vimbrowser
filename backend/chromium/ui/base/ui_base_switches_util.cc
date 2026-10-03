// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/ui_base_switches_util.h"

#include "base/command_line.h"
#include "build/build_config.h"
#include "ui/base/ui_base_features.h"
#include "ui/base/ui_base_switches.h"


namespace switches {

bool IsElasticOverscrollEnabledOnRoot() {
  return IsElasticOverscrollSupported();
}

bool IsElasticOverscrollSupported() {
// On macOS and iOS this value is adjusted in `UpdateScrollbarTheme()`,
// but the system default is true.
#if BUILDFLAG(IS_APPLE)
  return true;
#else
  return base::FeatureList::IsEnabled(features::kElasticOverscroll);
#endif
}

}  // namespace switches
