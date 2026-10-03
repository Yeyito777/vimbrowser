// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40285824): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "components/startup_metric_utils/browser/startup_metric_utils.h"

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "base/command_line.h"
#include "base/dcheck_is_on.h"
#include "base/location.h"
#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/threading/scoped_thread_priority.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "components/privacy_sandbox/privacy_sandbox_attestations/privacy_sandbox_attestations_histograms.h"


namespace {
const char kProcessType[] = "type";

startup_metric_utils::StartupTemperature g_startup_temperature =
    startup_metric_utils::UNDETERMINED_STARTUP_TEMPERATURE;

// Helper function for splitting out an UMA histogram based on startup
// temperature. |histogram_function| is the histogram type, and corresponds to
// an UMA function like base::UmaHistogramLongTimes. It must itself be a
// function that only takes two parameters.
// |basename| is the basename of the histogram. A histogram of this name will
// always be recorded to. If the startup temperature is known then a value
// will also be recorded to the histogram with name |basename| and suffix
// ".ColdStart", ".WarmStart" as appropriate.
// |value_expr| is an expression evaluating to the value to be recorded. This
// will be evaluated exactly once and cached, so side effects are not an
// issue. A metric logged using this function must have an affected-histogram
// entry in the definition of the StartupTemperature suffix in histograms.xml.
// This function must only be used in code that runs after
// |g_startup_temperature| has been initialized.
template <typename T>
void EmitHistogramWithTemperature(void (*histogram_function)(std::string_view,
                                                             T),
                                  std::string_view histogram_basename,
                                  T value) {
  // Always record to the base histogram.
  (*histogram_function)(histogram_basename, value);
  // Record to the cold/warm suffixed histogram as appropriate.
  switch (g_startup_temperature) {
    case startup_metric_utils::COLD_STARTUP_TEMPERATURE:
      (*histogram_function)(base::StrCat({histogram_basename, ".ColdStartup"}),
                            value);
      break;
    case startup_metric_utils::WARM_STARTUP_TEMPERATURE:
      (*histogram_function)(base::StrCat({histogram_basename, ".WarmStartup"}),
                            value);
      break;
    case startup_metric_utils::LUKEWARM_STARTUP_TEMPERATURE:
      // No suffix emitted for lukewarm startups.
      break;
    case startup_metric_utils::UNDETERMINED_STARTUP_TEMPERATURE:
      break;
    case startup_metric_utils::STARTUP_TEMPERATURE_COUNT:
      NOTREACHED();
  }
}

}  // namespace

namespace startup_metric_utils {

void BrowserStartupMetricRecorder::EmitHistogramWithTemperatureAndTraceEvent(
    void (*histogram_function)(std::string_view, base::TimeDelta),
    const char* histogram_basename,
    base::TimeTicks begin_ticks,
    base::TimeTicks end_ticks) {
  EmitHistogramWithTemperature(histogram_function, histogram_basename,
                               end_ticks - begin_ticks);
  GetCommon().EmitTraceEvent(histogram_basename, begin_ticks, end_ticks);
}

void BrowserStartupMetricRecorder::EmitBrowserWindowDisplayHistogram() {
  if (is_browser_window_display_metric_emitted_) {
    return;
  }

  // The metric requires the message loop to have started so that the startup
  // temperature evaluation has run.
  if (browser_window_display_ticks_.is_null() ||
      message_loop_start_ticks_.is_null()) {
    return;
  }

  // Skip logging if the main window startup was interrupted, e.g., by
  // --silent-launch, profile picker, or bad flags prompt.
  if (!ShouldLogStartupHistogram()) {
    return;
  }

  is_browser_window_display_metric_emitted_ = true;

  EmitHistogramWithTemperatureAndTraceEvent(
      &base::UmaHistogramLongTimes, "Startup.BrowserWindowDisplay",
      GetCommon().application_start_ticks_, browser_window_display_ticks_);
}

BrowserStartupMetricRecorder& GetBrowser() {
  // If this ceases to be true, Get{Common,Browser} need to be changed to use
  // base::NoDestructor.
  static_assert(
      std::is_trivially_destructible<BrowserStartupMetricRecorder>::value,
      "Startup metric recorder classes must be trivially destructible.");

  // This guard prevents non-browser processes from reporting browser process
  // metrics.
  CHECK(!base::CommandLine::ForCurrentProcess()->HasSwitch(kProcessType));
  static BrowserStartupMetricRecorder instance;
  return instance;
}


void BrowserStartupMetricRecorder::ResetSessionForTesting() {
  GetCommon().ResetSessionForTesting();
  // Reset global ticks that will be recorded multiple times when multiple
  // tests run in the same process.
  main_window_startup_interrupted_ = false;
  message_loop_start_ticks_ = base::TimeTicks();
  browser_window_display_ticks_ = base::TimeTicks();
  browser_window_first_paint_ticks_ = base::TimeTicks();
  is_privacy_sandbox_attestations_component_ready_recorded_ = false;
  is_privacy_sandbox_attestations_first_check_recorded_ = false;
  is_first_run_ = false;
  is_browser_window_display_metric_emitted_ = false;
}

bool BrowserStartupMetricRecorder::WasMainWindowStartupInterrupted() const {
  return main_window_startup_interrupted_;
}

void BrowserStartupMetricRecorder::SetNonBrowserUIDisplayed() {
  main_window_startup_interrupted_ = true;
}

void BrowserStartupMetricRecorder::SetBackgroundModeEnabled() {
  main_window_startup_interrupted_ = true;
}

void BrowserStartupMetricRecorder::RecordMessageLoopStartTicks(
    base::TimeTicks ticks) {
  DCHECK(message_loop_start_ticks_.is_null());
  message_loop_start_ticks_ = ticks;
  DCHECK(!message_loop_start_ticks_.is_null());
}

base::TimeTicks BrowserStartupMetricRecorder::GetWebContentsStartTicks() const {
  return web_contents_start_ticks_.is_null()
             ? GetCommon().application_start_ticks_
             : web_contents_start_ticks_;
}

void BrowserStartupMetricRecorder::RecordBrowserMainMessageLoopStart(
    base::TimeTicks ticks,
    bool is_first_run) {
  DCHECK(!GetCommon().application_start_ticks_.is_null());
  is_first_run_ = is_first_run;

  RecordMessageLoopStartTicks(ticks);

  // Keep RecordHardFaultHistogram() near the top of this method (as much as
  // possible) as many other histograms depend on it setting
  // |g_startup_temperature|.
  RecordHardFaultHistogram();

  // Record timing of the browser message-loop start time.
  if (is_first_run) {
    EmitHistogramWithTemperatureAndTraceEvent(
        &base::UmaHistogramLongTimes100,
        "Startup.BrowserMessageLoopStartTime.FirstRun",
        GetCommon().application_start_ticks_, ticks);
  } else {
    EmitHistogramWithTemperatureAndTraceEvent(
        &base::UmaHistogramLongTimes100, "Startup.BrowserMessageLoopStartTime",
        GetCommon().application_start_ticks_, ticks);
  }
  GetCommon().AddStartupEventsForTelemetry();

  // Record values stored prior to startup temperature evaluation.
  EmitBrowserWindowDisplayHistogram();

  // Process creation to application start. See comment above
  // RecordApplicationStart().
  if (!GetCommon().process_creation_ticks_.is_null()) {
    EmitHistogramWithTemperatureAndTraceEvent(
        &base::UmaHistogramLongTimes,
        "Startup.LoadTime.ProcessCreateToApplicationStart",
        GetCommon().process_creation_ticks_,
        GetCommon().application_start_ticks_);

    // Application start to ChromeMain().
    DCHECK(!GetCommon().chrome_main_entry_ticks_.is_null());
    EmitHistogramWithTemperatureAndTraceEvent(
        &base::UmaHistogramLongTimes,
        "Startup.LoadTime.ApplicationStartToChromeMain",
        GetCommon().application_start_ticks_,
        GetCommon().chrome_main_entry_ticks_);
  }

  // PreReadFile time.
  if (!GetCommon().preread_end_ticks_.is_null() &&
      !GetCommon().preread_begin_ticks_.is_null()) {
    EmitHistogramWithTemperatureAndTraceEvent(
        &base::UmaHistogramLongTimes, "Startup.Browser.LoadTime.PreReadFile",
        GetCommon().preread_begin_ticks_, GetCommon().preread_end_ticks_);
  }
}

void BrowserStartupMetricRecorder::RecordBrowserMainLoopFirstIdle(
    base::TimeTicks ticks) {
  DCHECK(!GetCommon().application_start_ticks_.is_null());
  GetCommon().AssertFirstCallInSession(FROM_HERE);

  if (!ShouldLogStartupHistogram()) {
    return;
  }

  EmitHistogramWithTemperatureAndTraceEvent(
      &base::UmaHistogramLongTimes100, "Startup.BrowserMessageLoopFirstIdle",
      GetCommon().application_start_ticks_, ticks);
}

void BrowserStartupMetricRecorder::RecordBrowserWindowDisplay(
    base::TimeTicks ticks) {
  DCHECK(!ticks.is_null());

  // Return if it has already been recorded.
  if (!browser_window_display_ticks_.is_null()) {
    return;
  }

  // The value will be recorded in appropriate histograms after the startup
  // temperature is evaluated.
  //
  // Note: In some cases (e.g. launching with --silent-launch), the first
  // browser window is displayed after the startup temperature is evaluated. In
  // these cases, the value will not be recorded, which is the desired behavior
  // for a non-conventional launch.
  browser_window_display_ticks_ = ticks;

  EmitBrowserWindowDisplayHistogram();
}

void BrowserStartupMetricRecorder::RecordBrowserWindowFirstPaintTicks(
    base::TimeTicks ticks) {
  DCHECK(!ticks.is_null());

  if (!browser_window_first_paint_ticks_.is_null()) {
    return;
  }

  browser_window_first_paint_ticks_ = ticks;
}

void BrowserStartupMetricRecorder::RecordFirstWebContentsNonEmptyPaint(
    base::TimeTicks now,
    base::TimeTicks render_process_host_init_time) {
  const base::TimeTicks web_contents_start_ticks = GetWebContentsStartTicks();
  DCHECK(!web_contents_start_ticks.is_null());
  GetCommon().AssertFirstCallInSession(FROM_HERE);

  if (!ShouldLogStartupHistogram()) {
    return;
  }

  EmitHistogramWithTemperatureAndTraceEvent(
      &base::UmaHistogramLongTimes100,
      "Startup.FirstWebContents.NonEmptyPaint3", web_contents_start_ticks, now);

  EmitHistogramWithTemperature(
      &base::UmaHistogramLongTimes100,
      "Startup.BrowserMessageLoopStart.To.NonEmptyPaint2",
      now - message_loop_start_ticks_);
}

void BrowserStartupMetricRecorder::RecordFirstWebContentsMainNavigationStart(
    base::TimeTicks ticks) {
  const base::TimeTicks web_contents_start_ticks = GetWebContentsStartTicks();
  DCHECK(!web_contents_start_ticks.is_null());
  GetCommon().AssertFirstCallInSession(FROM_HERE);

  if (!ShouldLogStartupHistogram()) {
    return;
  }

  EmitHistogramWithTemperatureAndTraceEvent(
      &base::UmaHistogramLongTimes100,
      "Startup.FirstWebContents.MainNavigationStart", web_contents_start_ticks,
      ticks);
}

void BrowserStartupMetricRecorder::RecordFirstWebContentsMainNavigationFinished(
    base::TimeTicks ticks) {
  const base::TimeTicks web_contents_start_ticks = GetWebContentsStartTicks();
  DCHECK(!web_contents_start_ticks.is_null());
  GetCommon().AssertFirstCallInSession(FROM_HERE);

  if (!ShouldLogStartupHistogram()) {
    return;
  }

  EmitHistogramWithTemperatureAndTraceEvent(
      &base::UmaHistogramLongTimes100,
      "Startup.FirstWebContents.MainNavigationFinished",
      web_contents_start_ticks, ticks);
}

void BrowserStartupMetricRecorder::RecordBrowserWindowFirstPaint(
    base::TimeTicks ticks) {
  static bool is_first_call = true;
  if (!is_first_call || ticks.is_null()) {
    return;
  }
  is_first_call = false;
  RecordBrowserWindowFirstPaintTicks(ticks);
  if (!ShouldLogStartupHistogram()) {
    return;
  }

  base::TimeTicks latency_origin = GetApplicationStartTicksForStartup();
  if (latency_origin.is_null()) {
    return;
  }
  DCHECK(!latency_origin.is_null());

  EmitHistogramWithTemperatureAndTraceEvent(&base::UmaHistogramLongTimes100,
                                            "Startup.BrowserWindow.FirstPaint",
                                            latency_origin, ticks);
}

void BrowserStartupMetricRecorder::RecordFirstRunSentinelCreation(
    FirstRunSentinelCreationResult result) {
  base::UmaHistogramEnumeration("FirstRun.Sentinel.Created", result);
}

void BrowserStartupMetricRecorder::RecordHardFaultHistogram() {
}

bool BrowserStartupMetricRecorder::ShouldLogStartupHistogram() const {
  return !WasMainWindowStartupInterrupted();
}

StartupTemperature BrowserStartupMetricRecorder::GetStartupTemperature() const {
  return g_startup_temperature;
}

bool BrowserStartupMetricRecorder::IsFirstRun() const {
  return is_first_run_;
}

base::TimeTicks
BrowserStartupMetricRecorder::GetApplicationStartTicksForStartup() const {
  return GetCommon().application_start_ticks_;
}


void BrowserStartupMetricRecorder::RecordExternalStartupMetric(
    const char* histogram_name,
    base::TimeTicks completion_ticks,
    bool set_non_browser_ui_displayed) {
  DCHECK(!GetCommon().application_start_ticks_.is_null());

  if (!ShouldLogStartupHistogram()) {
    return;
  }

  EmitHistogramWithTemperatureAndTraceEvent(
      &base::UmaHistogramMediumTimes, histogram_name,
      GetCommon().application_start_ticks_, completion_ticks);

  if (set_non_browser_ui_displayed) {
    SetNonBrowserUIDisplayed();
  }
}

// There are two possible callers of `ComponentReady()`:
// a) Component registration, when there is existing component file on disk.
// b) Component installation, when the component is downloaded.
//
// The following factors affect the timing of `ComponentReady()`:
// Non-browser UI during startup, for example, profile picker.
// - When the user stays at the profile picker indefinitely. The registration
// takes place in around 4 minutes after opening the browser.
//
// The purpose of this metric is to understand the time gap between the time
// users are able to navigate and the time the Privacy Sandbox attestations map
// is ready. If navigation to sites that use Privacy Sandbox APIs takes place
// during this gap, the API calls may be rejected because the attestations map
// has not been ready yet.
//
// To reduce the noise introduced by non-browser UI, we measure from the first
// browser window paint if it has been recorded. If it is not recorded, the
// measurement is taken from application start.
// TODO(crbug.com/329235182): The Privacy Sandbox Attestation start up related
// histograms are not using the temperature breakouts. The logic for all these
// histograms could just live in the privacy sandbox component itself, which
// pulls from startup code just to get the application start timeticks.
void BrowserStartupMetricRecorder::RecordPrivacySandboxAttestationsFirstReady(
    base::TimeTicks ticks) {
  DCHECK(!ticks.is_null());

  // This metric should be recorded at most once for each Chrome session.
  if (is_privacy_sandbox_attestations_component_ready_recorded_) {
    return;
  }

  // The first browser window paint has been recorded.
  if (!browser_window_first_paint_ticks_.is_null()) {
    is_privacy_sandbox_attestations_component_ready_recorded_ = true;
    base::UmaHistogramLongTimes100(
        privacy_sandbox::kComponentReadyFromBrowserWindowFirstPaintUMA,
        ticks - browser_window_first_paint_ticks_);
    return;
  }

  // Otherwise, this implies the component is installed before first browser
  // window paint.
  is_privacy_sandbox_attestations_component_ready_recorded_ = true;
  if (WasMainWindowStartupInterrupted()) {
    // The durations should be a few minutes.
    base::UmaHistogramLongTimes100(
        privacy_sandbox::kComponentReadyFromApplicationStartWithInterruptionUMA,
        ticks - GetCommon().application_start_ticks_);
  } else {
    // The durations should be a few milliseconds.
    base::UmaHistogramLongTimes100(
        privacy_sandbox::kComponentReadyFromApplicationStartUMA,
        ticks - GetCommon().application_start_ticks_);
  }
}

void BrowserStartupMetricRecorder::RecordPrivacySandboxAttestationFirstCheck(
    base::TimeTicks ticks) {
  DCHECK(!ticks.is_null());

  // This metric should be recorded at most once for each Chrome session.
  if (is_privacy_sandbox_attestations_first_check_recorded_) {
    return;
  }

  is_privacy_sandbox_attestations_first_check_recorded_ = true;

  // Record the first time a Privacy Sandbox API is checked for attestation.
  base::UmaHistogramLongTimes100(privacy_sandbox::kAttestationFirstCheckTimeUMA,
                                 ticks - GetCommon().application_start_ticks_);
}

}  // namespace startup_metric_utils
