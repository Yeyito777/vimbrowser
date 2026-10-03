// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/ui/screen_info_metrics_provider.h"

#include <algorithm>

#include "build/build_config.h"
#include "third_party/metrics_proto/system_profile.pb.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"


namespace metrics {


ScreenInfoMetricsProvider::ScreenInfoMetricsProvider() {
}

ScreenInfoMetricsProvider::~ScreenInfoMetricsProvider() {
}

void ScreenInfoMetricsProvider::ProvideSystemProfileMetrics(
    SystemProfileProto* system_profile_proto) {
  // This may be called before the screen info has been initialized, such as
  // when the persistent system profile gets filled in initially.
  const std::optional<gfx::Size> display_size = GetScreenSize();
  if (!display_size.has_value())
    return;

  SystemProfileProto::Hardware* hardware =
      system_profile_proto->mutable_hardware();

  hardware->set_primary_screen_width(display_size->width());
  hardware->set_primary_screen_height(display_size->height());
  hardware->set_primary_screen_scale_factor(GetScreenDeviceScaleFactor());
  hardware->set_screen_count(GetScreenCount());

}

std::optional<gfx::Size> ScreenInfoMetricsProvider::GetScreenSize() const {
  auto* screen = display::Screen::Get();
  if (!screen)
    return std::nullopt;
  return std::make_optional(screen->GetPrimaryDisplay().GetSizeInPixel());
}

float ScreenInfoMetricsProvider::GetScreenDeviceScaleFactor() const {
  return display::Screen::Get()->GetPrimaryDisplay().device_scale_factor();
}

int ScreenInfoMetricsProvider::GetScreenCount() const {
  return display::Screen::Get()->GetNumDisplays();
}

}  // namespace metrics
