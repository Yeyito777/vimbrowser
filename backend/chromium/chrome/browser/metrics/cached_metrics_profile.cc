// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/metrics/cached_metrics_profile.h"

#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"


namespace metrics {
namespace {


}  // namespace

CachedMetricsProfile::CachedMetricsProfile() = default;

CachedMetricsProfile::~CachedMetricsProfile() = default;

Profile* CachedMetricsProfile::GetMetricsProfile() {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  if (!profile_manager)
    return nullptr;

  // If there is a cached profile, reuse that.  However, check that it is still
  // valid first. This logic is valid for all platforms, including ChromeOS Ash.
  if (cached_profile_ && profile_manager->IsValidProfile(cached_profile_))
    return cached_profile_;

  // Find a suitable profile to use, and cache it so that we continue to report
  // statistics on the same profile.
  cached_profile_ = profile_manager->GetLastUsedProfileIfLoaded();
  if (cached_profile_) {
    // Ensure that the returned profile is not an incognito profile.
    cached_profile_ = cached_profile_->GetOriginalProfile();
  }
  return cached_profile_;
}

}  // namespace metrics
