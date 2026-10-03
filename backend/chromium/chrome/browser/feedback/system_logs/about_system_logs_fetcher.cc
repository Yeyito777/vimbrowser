// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/feedback/system_logs/about_system_logs_fetcher.h"

#include <memory>

#include "build/build_config.h"
#include "chrome/browser/feedback/system_logs/log_sources/chrome_internal_log_source.h"
#include "chrome/browser/feedback/system_logs/log_sources/device_event_log_source.h"
#include "chrome/browser/feedback/system_logs/log_sources/memory_details_log_source.h"
#include "chrome/browser/feedback/system_logs/log_sources/related_website_sets_source.h"
#include "chrome/browser/first_party_sets/first_party_sets_policy_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/feedback/system_logs/system_logs_fetcher.h"
#include "net/net_buildflags.h"

#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)
#include "chrome/browser/feedback/system_logs/log_sources/chrome_root_store_log_source.h"
#endif


#if BUILDFLAG(IS_LINUX)
#include "chrome/browser/feedback/system_logs/log_sources/ozone_platform_state_dump_source.h"
#endif

namespace system_logs {

SystemLogsFetcher* BuildAboutSystemLogsFetcher(content::WebUI* web_ui) {
  const bool scrub_data = false;
  // We aren't anonymizing, so we can pass null for the 1st party IDs.
  SystemLogsFetcher* fetcher = new SystemLogsFetcher(scrub_data);

  fetcher->AddSource(std::make_unique<ChromeInternalLogSource>());
  fetcher->AddSource(std::make_unique<DeviceEventLogSource>());
  fetcher->AddSource(std::make_unique<MemoryDetailsLogSource>());
  fetcher->AddSource(std::make_unique<RelatedWebsiteSetsSource>(
      first_party_sets::FirstPartySetsPolicyServiceFactory::
          GetForBrowserContext(Profile::FromWebUI(web_ui))));

#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)
  fetcher->AddSource(std::make_unique<ChromeRootStoreLogSource>());
#endif


#if BUILDFLAG(IS_LINUX)
  fetcher->AddSource(std::make_unique<OzonePlatformStateDumpSource>());
#endif  // BUILDFLAG(IS_LINUX)

  return fetcher;
}

}  // namespace system_logs
