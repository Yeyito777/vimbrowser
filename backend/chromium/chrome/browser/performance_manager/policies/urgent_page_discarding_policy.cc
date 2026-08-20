// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/performance_manager/policies/urgent_page_discarding_policy.h"

#include <memory>

#include "base/feature_list.h"
#include "base/time/time.h"
#include "chrome/browser/performance_manager/policies/page_discarding_helper.h"
#include "chrome/browser/performance_manager/policies/policy_features.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"


namespace performance_manager::policies {

namespace {

bool g_disabled_for_testing = false;


}  // namespace

UrgentPageDiscardingPolicy::UrgentPageDiscardingPolicy()
    : sustained_memory_pressure_timer_(
          FROM_HERE,
          base::Seconds(5),
          base::BindRepeating(
              &UrgentPageDiscardingPolicy::HandleMemoryPressureEvent,
              base::Unretained(this))) {
  if (base::FeatureList::IsEnabled(features::kSustainedPMUrgentDiscarding)) {
    sustained_memory_pressure_evaluator_.emplace(base::BindRepeating(
        &UrgentPageDiscardingPolicy::OnSustainedMemoryPressure,
        base::Unretained(this)));
  } else {
    memory_pressure_listener_registration_.emplace(
        FROM_HERE, base::MemoryPressureListenerTag::kUrgentPageDiscardingPolicy,
        this);
  }
}
UrgentPageDiscardingPolicy::~UrgentPageDiscardingPolicy() = default;

void UrgentPageDiscardingPolicy::OnPassedToGraph(Graph* graph) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(PageDiscardingHelper::GetFromGraph(graph))
      << "A PageDiscardingHelper instance should be registered against the "
         "graph in order to use this policy.";
}

void UrgentPageDiscardingPolicy::OnTakenFromGraph(Graph* graph) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}


void UrgentPageDiscardingPolicy::DisableForTesting() {
  g_disabled_for_testing = true;
}

void UrgentPageDiscardingPolicy::OnMemoryPressure(
    base::MemoryPressureLevel new_level) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (new_level != base::MEMORY_PRESSURE_LEVEL_CRITICAL) {
    return;
  }

  HandleMemoryPressureEvent();
}

void UrgentPageDiscardingPolicy::OnSustainedMemoryPressure(
    bool is_sustained_memory_pressure) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (is_sustained_memory_pressure) {
    HandleMemoryPressureEvent();
    // Start the time that will continuously discard a tab while under sustained
    // memory pressure.
    sustained_memory_pressure_timer_.Reset();
  } else {
    sustained_memory_pressure_timer_.Stop();
  }
}

void UrgentPageDiscardingPolicy::HandleMemoryPressureEvent() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (g_disabled_for_testing) {
    return;
  }

  // Don't discard a page if urgent discarding is disabled. The feature state is
  // checked here instead of at policy creation time so that only clients that
  // experience memory pressure are enrolled in the experiment.
  if (!base::FeatureList::IsEnabled(
          performance_manager::features::kUrgentPageDiscarding)) {
    return;
  }

  PageDiscardingHelper::GetFromGraph(GetOwningGraph())
      ->DiscardAPage(DiscardEligibilityPolicy::DiscardReason::URGENT);
}

}  // namespace performance_manager::policies
