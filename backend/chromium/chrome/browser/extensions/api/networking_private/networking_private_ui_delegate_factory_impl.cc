// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/networking_private/networking_private_ui_delegate_factory_impl.h"

#include <memory>

#include "build/build_config.h"
#include "build/chromeos_buildflags.h"


namespace extensions {

std::unique_ptr<NetworkingPrivateDelegate::UIDelegate>
NetworkingPrivateUIDelegateFactoryImpl::CreateDelegate() {
  return nullptr;
}

}  // namespace extensions
