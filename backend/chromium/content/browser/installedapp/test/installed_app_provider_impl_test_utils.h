// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_INSTALLEDAPP_TEST_INSTALLED_APP_PROVIDER_IMPL_TEST_UTILS_H_
#define CONTENT_BROWSER_INSTALLEDAPP_TEST_INSTALLED_APP_PROVIDER_IMPL_TEST_UTILS_H_

#include <optional>
#include <vector>

#include "content/public/browser/content_browser_client.h"
#include "third_party/blink/public/mojom/installedapp/related_application.mojom-forward.h"
#include "url/gurl.h"


namespace content {

blink::mojom::RelatedApplicationPtr CreateRelatedApplicationFromPlatformAndId(
    const std::string& platform,
    const std::string& id);


class FakeContentBrowserClientForQueryInstalledWebApps
    : public ContentBrowserClient {
 public:
  explicit FakeContentBrowserClientForQueryInstalledWebApps(
      std::vector<std::string> installed_web_app_ids);
  ~FakeContentBrowserClientForQueryInstalledWebApps() override;

  void QueryInstalledWebAppsByManifestId(
      const GURL&,
      const GURL& id,
      content::BrowserContext*,
      base::OnceCallback<void(std::optional<blink::mojom::RelatedApplication>)>
          callback) override;

 private:
  std::vector<GURL> installed_web_app_ids_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_INSTALLEDAPP_TEST_INSTALLED_APP_PROVIDER_IMPL_TEST_UTILS_H_
