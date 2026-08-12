// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chrome_browser_field_trials.h"

#include <optional>
#include <string>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/features.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/field_trial_params.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/metrics/chrome_browser_sampling_trials.h"
#include "chrome/browser/metrics/chrome_metrics_service_accessor.h"
#include "chrome/browser/metrics/chrome_metrics_service_client.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "components/feed/feed_feature_list.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/metrics/persistent_histograms.h"
#include "components/site_isolation/features.h"
#include "components/variations/feature_overrides.h"
#include "components/version_info/version_info.h"
#include "third_party/blink/public/common/features.h"


#if BUILDFLAG(IS_CHROMEOS)
#include "chrome/common/channel_info.h"
#include "chromeos/ash/services/multidevice_setup/public/cpp/first_run_field_trial.h"
#endif

#if BUILDFLAG(IS_LINUX)
#include "base/nix/xdg_util.h"
#include "ui/base/ui_base_features.h"
#endif  // BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
#include "base/check_deref.h"
#include "chrome/browser/first_run/first_run.h"
#include "chrome/browser/signin/before_fre_refresh_hats_field_trial.h"
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)

ChromeBrowserFieldTrials::ChromeBrowserFieldTrials(PrefService* local_state)
    : local_state_(local_state) {
  DCHECK(local_state_);
}

ChromeBrowserFieldTrials::~ChromeBrowserFieldTrials() = default;

void ChromeBrowserFieldTrials::SetUpClientSideFieldTrials(
    bool has_seed,
    const variations::EntropyProviders& entropy_providers,
    base::FeatureList* feature_list) {
  // Only create the fallback trials if there isn't already a variations seed
  // being applied. This should occur during first run when first-run variations
  // isn't supported. It's assumed that, if there is a seed, then it either
  // contains the relevant studies, or is intentionally omitted, so no fallback
  // is needed. The exception is for sampling trials. Fallback trials are
  // created even if no variations seed was applied. This allows testing the
  // fallback code by intentionally omitting the sampling trial from a
  // variations seed.
  metrics::CreateFallbackSamplingTrialsIfNeeded(
      entropy_providers.default_entropy(), feature_list);
  metrics::CreateFallbackUkmSamplingTrialIfNeeded(
      entropy_providers.default_entropy(), feature_list);

#if BUILDFLAG(IS_CHROMEOS)
  if (!has_seed) {
    ash::multidevice_setup::CreateFirstRunFieldTrial(feature_list);
  }
#endif

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
  // This trial is client controlled on Mac and Linux because the survey is
  // triggered on the very first run of Chrome. These platforms do not support
  // variations seed on the first run.
  if (first_run::IsChromeFirstRun()) {
    signin::CreateBeforeFreRefreshHatsFieldTrial(
        CHECK_DEREF(feature_list), entropy_providers.default_entropy());
  }
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
}

void ChromeBrowserFieldTrials::RegisterSyntheticTrials() {
}

void ChromeBrowserFieldTrials::RegisterFeatureOverrides(
    base::FeatureList* feature_list) {
  variations::FeatureOverrides feature_overrides(*feature_list);

#if BUILDFLAG(IS_LINUX)
  // On Linux/Desktop platform variants, such as ozone/wayland, some features
  // might need to be disabled as per OzonePlatform's runtime properties.
  // OzonePlatform selection and initialization, in turn, depend on Chrome flags
  // processing, namely 'ozone-platform', so do it here.
  //
  // TODO(nickdiego): Move it back to
  // ChromeMainDelegate::PostEarlyInitialization.

  std::unique_ptr<base::Environment> env = base::Environment::Create();
  std::string xdg_session_type =
      env->GetVar(base::nix::kXdgSessionTypeEnvVar).value_or(std::string());

  if (xdg_session_type == "wayland") {
    feature_overrides.DisableFeature(features::kEyeDropper);
  }
#elif 0 // BUILDFLAG(IS_LINUX)
  // Desktop-first features which are past incubation should either end up here,
  // or to a finch trial that enables it for all form factors.
#endif  // BUILDFLAG(IS_ANDROID)
}
