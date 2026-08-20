// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TRACING_CHROME_TRACING_DELEGATE_H_
#define CHROME_BROWSER_TRACING_CHROME_TRACING_DELEGATE_H_

#include <optional>

#include "base/no_destructor.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "content/public/browser/tracing_delegate.h"

#include "chrome/browser/ui/browser_list_observer.h"

namespace tracing {
class BackgroundTracingStateManager;
}

// These values are logged to UMA. Entries should not be renumbered and numeric
// values should never be reused. Please keep in sync with
// "TracingFinalizationDisallowedReason" in
// src/tools/metrics/histograms/enums.xml.
enum class TracingFinalizationDisallowedReason {
  kIncognitoLaunched = 0,
  //  kProfileNotLoaded = 1, Obsolete
  //  kCrashMetricsNotLoaded = 2, Obsolete
  //  kLastSessionCrashed = 3, Obsolete
  //  kMetricsReportingDisabled = 4, Obsolete
  //  kTraceUploadedRecently = 5, Obsolete
  //  kLastTracingSessionDidNotEnd = 6, Obsolete as of Nov'2024.
  kMaxValue = kIncognitoLaunched
};

class ChromeTracingDelegate : public content::TracingDelegate,
                              public BrowserListObserver
{
 public:
  // Whether system-wide performance trace collection using the external system
  // tracing service is enabled.
  static bool IsSystemWideTracingEnabled();

  ChromeTracingDelegate();
  ~ChromeTracingDelegate() override;

  // content::TracingDelegate implementation:
  bool IsRecordingAllowed(bool requires_anonymized_data,
                          base::TimeTicks session_start) const override;
  bool ShouldSaveUnuploadedTrace() const override;
  std::unique_ptr<tracing::BackgroundTracingStateManager> CreateStateManager()
      override;
  std::string RecordSerializedSystemProfileMetrics() const override;
  tracing::MetadataDataSource::BundleRecorder
  CreateSystemProfileMetadataRecorder() const override;


 private:
  // BrowserListObserver implementation.
  void OnBrowserAdded(Browser* browser) override;
  void OnBrowserRemoved(Browser* browser) override;

  // Track the most recent OffTheRecord browser creation time. It's ok to update
  // to a newer timestamp when there are multiple OffTheRecord browsers, since
  // the newer timestamp is stricter.
  base::TimeTicks latest_incognito_launched_;
};

#endif  // CHROME_BROWSER_TRACING_CHROME_TRACING_DELEGATE_H_
