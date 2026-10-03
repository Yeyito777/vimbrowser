// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_APPS_APP_SERVICE_PUBLISHERS_PUBLISHER_HOST_IMPL_H_
#define CHROME_BROWSER_APPS_APP_SERVICE_PUBLISHERS_PUBLISHER_HOST_IMPL_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "chrome/browser/apps/app_service/app_service_proxy_forward.h"
#include "chrome/browser/apps/app_service/publisher_host.h"

namespace web_app {
class WebApps;
}  // namespace web_app

namespace apps {

class ExtensionApps;

// PublisherHostImpl saves publishers created by AppServiceProxy.
class PublisherHostImpl : public PublisherHost {
 public:
  explicit PublisherHostImpl(AppServiceProxy* proxy);
  PublisherHostImpl(const PublisherHostImpl&) = delete;
  PublisherHostImpl& operator=(const PublisherHostImpl&) = delete;
  ~PublisherHostImpl() override;


 private:
  void Initialize();

  // Owns this class.
  raw_ptr<AppServiceProxy> proxy_;

  std::unique_ptr<web_app::WebApps> web_apps_;
  std::unique_ptr<ExtensionApps> chrome_apps_;
};


}  // namespace apps

#endif  // CHROME_BROWSER_APPS_APP_SERVICE_PUBLISHERS_PUBLISHER_HOST_IMPL_H_
