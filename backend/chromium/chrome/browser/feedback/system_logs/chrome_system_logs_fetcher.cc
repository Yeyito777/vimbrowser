// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/feedback/system_logs/chrome_system_logs_fetcher.h"

#include <memory>

#include "base/containers/span.h"
#include "build/build_config.h"
#include "chrome/browser/feedback/system_logs/log_sources/chrome_internal_log_source.h"
#include "chrome/browser/feedback/system_logs/log_sources/crash_ids_source.h"
#include "chrome/browser/feedback/system_logs/log_sources/memory_details_log_source.h"
#include "chrome/browser/feedback/system_logs/log_sources/performance_log_source.h"
#include "chrome/common/extensions/extension_constants.h"
#include "components/feedback/system_logs/system_logs_fetcher.h"
#include "components/supervised_user/core/common/buildflags.h"


#if BUILDFLAG(ENABLE_SUPERVISED_USERS)
#include "chrome/browser/feedback/system_logs/log_sources/family_info_log_source.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#endif

#if BUILDFLAG(IS_LINUX)
#include "chrome/browser/feedback/system_logs/log_sources/ozone_platform_state_dump_source.h"
#endif

namespace system_logs {

SystemLogsFetcher* BuildChromeSystemLogsFetcher(Profile* profile,
                                                bool scrub_data) {
  SystemLogsFetcher* fetcher = new SystemLogsFetcher(
      scrub_data, base::span(extension_misc::kBuiltInFirstPartyExtensionIds));

  fetcher->AddSource(std::make_unique<ChromeInternalLogSource>());
  fetcher->AddSource(std::make_unique<CrashIdsSource>());
  fetcher->AddSource(std::make_unique<MemoryDetailsLogSource>());
  fetcher->AddSource(std::make_unique<PerformanceLogSource>());

#if BUILDFLAG(ENABLE_SUPERVISED_USERS)
  signin::IdentityManager* identity_manager =
      IdentityManagerFactory::GetForProfile(profile);
  // Identity manager is not available for Guest profile in ChromeOS ash.
  if (identity_manager) {
    fetcher->AddSource(std::make_unique<FamilyInfoLogSource>(
        identity_manager, profile->GetURLLoaderFactory(),
        *profile->GetPrefs()));
  }
#endif


#if BUILDFLAG(IS_LINUX)
  fetcher->AddSource(std::make_unique<OzonePlatformStateDumpSource>());
#endif  // BUILDFLAG(IS_LINUX)

  return fetcher;
}

}  // namespace system_logs
