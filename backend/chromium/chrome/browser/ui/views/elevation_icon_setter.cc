// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/elevation_icon_setter.h"

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/views/controls/button/label_button.h"


// ElevationIconSetter --------------------------------------------------------

ElevationIconSetter::ElevationIconSetter(views::LabelButton* button)
    : button_(button) {
}

ElevationIconSetter::~ElevationIconSetter() = default;

void ElevationIconSetter::SetButtonIcon(const SkBitmap& icon) {
  if (!icon.isNull()) {
    float device_scale_factor = 1.0f;
    button_->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromImageSkia(
            gfx::ImageSkia::CreateFromBitmap(icon, device_scale_factor)));
  }
}
