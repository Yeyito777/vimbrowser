// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/app_service/publishers/publisher_host_impl.h"

#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/publishers/extension_apps.h"
#include "chrome/browser/web_applications/app_service/web_apps.h"


namespace apps {

namespace {


}  // anonymous namespace

PublisherHostImpl::PublisherHostImpl(AppServiceProxy* proxy) : proxy_(proxy) {
  DCHECK(proxy);
  Initialize();
}

PublisherHostImpl::~PublisherHostImpl() = default;


void PublisherHostImpl::Initialize() {
  web_apps_ = std::make_unique<web_app::WebApps>(proxy_);

  chrome_apps_ = std::make_unique<ExtensionApps>(proxy_);
  chrome_apps_->Initialize();
}


}  // namespace apps
