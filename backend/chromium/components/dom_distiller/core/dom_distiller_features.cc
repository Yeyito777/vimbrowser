// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/dom_distiller/core/dom_distiller_features.h"

#include <string>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "components/dom_distiller/core/dom_distiller_switches.h"
#include "components/dom_distiller/core/pref_names.h"


namespace dom_distiller {

bool IsDomDistillerEnabled() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableDomDistiller);
}

bool ShouldStartDistillabilityService() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableDistillabilityService);
}

BASE_FEATURE(kReaderModeUseReadability, base::FEATURE_ENABLED_BY_DEFAULT);

#if !BUILDFLAG(IS_IOS)
constexpr base::FeatureParam<bool> kReaderModeUseReadabilityUseDistiller{
    &kReaderModeUseReadability, /*name=*/"use_distiller",
    /*default_value=*/true};
#endif
constexpr base::FeatureParam<int> kReaderModeUseReadabilityHeuristicMinScore{
    &kReaderModeUseReadability, /*name=*/"heuristic_min_score",
#if BUILDFLAG(IS_IOS)
    /*default_value=*/50
#else
    /*default_value=*/100
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
};
constexpr base::FeatureParam<int>
    kReaderModeUseReadabilityHeuristicMinContentLength{
        &kReaderModeUseReadability, /*name=*/"heuristic_min_content_length",
#if BUILDFLAG(IS_IOS)
        /*default_value=*/160
#else
        /*default_value=*/200
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
    };
constexpr base::FeatureParam<int> kReaderModeUseReadabilityMinContentLength{
    &kReaderModeUseReadability, /*name=*/"min_content_length",
    /*default_value=*/0
};

bool ShouldUseReadabilityDistiller() {
#if BUILDFLAG(IS_IOS)
  return base::FeatureList::IsEnabled(kReaderModeUseReadability);
#else
  return base::FeatureList::IsEnabled(kReaderModeUseReadability) &&
         kReaderModeUseReadabilityUseDistiller.Get();
#endif
}

int GetReadabilityHeuristicMinScore() {
  return kReaderModeUseReadabilityHeuristicMinScore.Get();
}

int GetReadabilityHeuristicMinContentLength() {
  return kReaderModeUseReadabilityHeuristicMinContentLength.Get();
}

int GetMinimumAllowableDistilledContentLength() {
  return base::FeatureList::IsEnabled(kReaderModeUseReadability)
             ? kReaderModeUseReadabilityMinContentLength.Get()
             : 0;
}

#if BUILDFLAG(IS_IOS)
BASE_FEATURE(kReaderModeSupportNewFonts, base::FEATURE_ENABLED_BY_DEFAULT);
#endif


}  // namespace dom_distiller
