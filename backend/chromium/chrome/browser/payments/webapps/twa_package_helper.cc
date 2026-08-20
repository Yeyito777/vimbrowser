// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/payments/webapps/twa_package_helper.h"

#include <utility>

#include "base/location.h"
#include "base/task/single_thread_task_runner.h"


namespace {


}  // namespace

namespace payments {

TwaPackageHelper::TwaPackageHelper(
    content::RenderFrameHost* render_frame_host) {
}

TwaPackageHelper::~TwaPackageHelper() = default;

void TwaPackageHelper::GetTwaPackageName(
    GetTwaPackageNameCallback callback) const {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), twa_package_name_));
}

}  // namespace payments
