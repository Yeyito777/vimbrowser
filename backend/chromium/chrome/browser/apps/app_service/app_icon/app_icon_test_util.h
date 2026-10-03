// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_APPS_APP_SERVICE_APP_ICON_APP_ICON_TEST_UTIL_H_
#define CHROME_BROWSER_APPS_APP_SERVICE_APP_ICON_APP_ICON_TEST_UTIL_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "components/services/app_service/public/cpp/icon_types.h"
#include "extensions/grit/extensions_browser_resources.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"


namespace gfx {
class ImageSkia;
}

namespace apps {

inline constexpr int kSizeInDip = 64;

void EnsureRepresentationsLoaded(gfx::ImageSkia& output_image_skia);

void LoadDefaultIcon(gfx::ImageSkia& output_image_skia,
                     int resource_id = IDR_APP_DEFAULT_ICON);

void VerifyIcon(const gfx::ImageSkia& src, const gfx::ImageSkia& dst);

void VerifyCompressedIcon(const std::vector<uint8_t>& src_data,
                          const apps::IconValue& icon);

gfx::ImageSkia CreateSquareIconImageSkia(int size_dp, SkColor solid_color);


}  // namespace apps

#endif  // CHROME_BROWSER_APPS_APP_SERVICE_APP_ICON_APP_ICON_TEST_UTIL_H_
