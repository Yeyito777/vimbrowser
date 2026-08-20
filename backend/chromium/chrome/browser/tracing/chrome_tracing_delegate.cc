// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/tracing/chrome_tracing_delegate.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/json/values_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/trace_event/named_trigger.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_otr_state.h"
#include "chrome/common/pref_names.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/tracing/common/background_tracing_metrics_provider.h"
#include "components/tracing/common/background_tracing_state_manager.h"
#include "components/tracing/common/background_tracing_utils.h"
#include "components/tracing/common/system_profile_metadata_recorder.h"
#include "components/tracing/common/tracing_scenarios_config.h"
#include "components/variations/active_field_trials.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_thread.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/tracing/public/cpp/tracing_features.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"



namespace {

using tracing::BackgroundTracingStateManager;

}  // namespace

ChromeTracingDelegate::ChromeTracingDelegate() {
  // Ensure that this code is called on the UI thread, except for
  // tests where a UI thread might not have been initialized at this point.
  DCHECK(
      content::BrowserThread::CurrentlyOn(content::BrowserThread::UI) ||
      !content::BrowserThread::IsThreadInitialized(content::BrowserThread::UI));
  BrowserList::AddObserver(this);
}

ChromeTracingDelegate::~ChromeTracingDelegate() {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  BrowserList::RemoveObserver(this);
}


void ChromeTracingDelegate::OnBrowserAdded(Browser* browser) {
  if (browser->profile()->IsOffTheRecord()) {
    latest_incognito_launched_ = base::TimeTicks::Now();
    base::trace_event::EmitNamedTrigger("incognito-start");
  }
}

void ChromeTracingDelegate::OnBrowserRemoved(Browser* browser) {
  if (!IsOffTheRecordSessionActive()) {
    base::trace_event::EmitNamedTrigger("incognito-end");
  }
}


bool ChromeTracingDelegate::IsRecordingAllowed(
    bool requires_anonymized_data,
    base::TimeTicks session_start) const {
  // If the background tracing is specified on the command-line, we allow
  // any scenario to be traced and uploaded.
  if (!requires_anonymized_data) {
    return true;
  }

  if (IsOffTheRecordSessionActive() ||
      session_start <= latest_incognito_launched_) {
    UMA_HISTOGRAM_ENUMERATION(
        "Tracing.Background.FinalizationDisallowedReason",
        TracingFinalizationDisallowedReason::kIncognitoLaunched);
    return false;
  }

  return true;
}

bool ChromeTracingDelegate::ShouldSaveUnuploadedTrace() const {
  return true;
}

std::unique_ptr<tracing::BackgroundTracingStateManager>
ChromeTracingDelegate::CreateStateManager() {
  return tracing::BackgroundTracingStateManager::CreateInstance(
      g_browser_process->local_state());
}

std::string ChromeTracingDelegate::RecordSerializedSystemProfileMetrics()
    const {
  metrics::SystemProfileProto system_profile_proto;
  auto recorder = tracing::BackgroundTracingMetricsProvider::
      GetSystemProfileMetricsRecorder();
  if (!recorder) {
    return std::string();
  }
  recorder.Run(system_profile_proto);
  std::string serialized_system_profile;
  system_profile_proto.SerializeToString(&serialized_system_profile);
  return serialized_system_profile;
}

tracing::MetadataDataSource::BundleRecorder
ChromeTracingDelegate::CreateSystemProfileMetadataRecorder() const {
  return base::BindRepeating(&tracing::RecordSystemProfileMetadata);
}


bool ChromeTracingDelegate::IsSystemWideTracingEnabled() {
  return false;
}
