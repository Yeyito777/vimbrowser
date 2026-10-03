// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/web_applications/share_target_utils.h"

#include <algorithm>
#include <optional>

#include "base/memory/scoped_refptr.h"
#include "base/notimplemented.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/web_share_target/target_util.h"
#include "components/services/app_service/public/cpp/intent_util.h"
#include "components/services/app_service/public/cpp/share_target.h"
#include "extensions/common/constants.h"
#include "net/base/filename_util.h"
#include "net/base/mime_util.h"
#include "services/network/public/cpp/resource_request_body.h"
#include "storage/browser/file_system/file_system_context.h"
#include "storage/browser/file_system/file_system_url.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"


namespace web_app {

std::vector<SharedField> ExtractSharedFields(
    const apps::ShareTarget& share_target,
    const apps::Intent& intent) {
  std::vector<SharedField> result;

  if (!share_target.params.title.empty() && intent.share_title.has_value() &&
      !intent.share_title->empty()) {
    result.push_back(
        {.name = share_target.params.title, .value = *intent.share_title});
  }

  if (!intent.share_text.has_value()) {
    return result;
  }

  apps_util::SharedText extracted_text =
      apps_util::ExtractSharedText(*intent.share_text);

  if (!share_target.params.text.empty() && !extracted_text.text.empty()) {
    result.push_back(
        {.name = share_target.params.text, .value = extracted_text.text});
  }

  if (!share_target.params.url.empty() && !extracted_text.url.is_empty()) {
    result.push_back(
        {.name = share_target.params.url, .value = extracted_text.url.spec()});
  }

  return result;
}

NavigateParams NavigateParamsForShareTarget(
    Browser* browser,
    const apps::ShareTarget& share_target,
    const apps::Intent& intent,
    const std::vector<base::FilePath>& launch_files) {
  NavigateParams nav_params(browser, share_target.action,
                            ui::PAGE_TRANSITION_AUTO_TOPLEVEL);

  // TODO(crbug.com/40158988): Support Web Share Target on Windows.
  // TODO(crbug.com/40734106): Support Web Share Target on Mac.
  NOTIMPLEMENTED();

  return nav_params;
}

}  // namespace web_app
