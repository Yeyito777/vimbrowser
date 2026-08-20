// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/chrome_device_id_helper.h"

#include <string>

#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "components/signin/public/base/device_id_helper.h"


namespace {
}  // namespace

std::string GetSigninScopedDeviceIdForProfile(Profile* profile) {
  return signin::GetSigninScopedDeviceId(profile->GetPrefs());
}
