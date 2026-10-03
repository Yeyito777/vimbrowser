// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_METRICS_CONTENT_CONTENT_STABILITY_METRICS_PROVIDER_H_
#define COMPONENTS_METRICS_CONTENT_CONTENT_STABILITY_METRICS_PROVIDER_H_

#include <memory>

#include "base/gtest_prod_util.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "build/build_config.h"
#include "components/metrics/metrics_provider.h"
#include "components/metrics/stability_metrics_helper.h"
#include "content/public/browser/browser_child_process_observer.h"
#include "content/public/browser/render_process_host_creation_observer.h"
#include "content/public/browser/render_process_host_observer.h"


class PrefService;

namespace metrics {

class ExtensionsHelper;

// ContentStabilityMetricsProvider gathers and logs Chrome-specific stability-
// related metrics.
class ContentStabilityMetricsProvider
    : public MetricsProvider,
      public content::BrowserChildProcessObserver,
      public content::RenderProcessHostCreationObserver,
      public content::RenderProcessHostObserver {
 public:
  // |extensions_helper| is used to determine if a process corresponds to an
  // extension and is optional. If an ExtensionsHelper is not supplied it is
  // assumed the process does not correspond to an extension.
  ContentStabilityMetricsProvider(
      PrefService* local_state,
      std::unique_ptr<ExtensionsHelper> extensions_helper);
  ContentStabilityMetricsProvider(const ContentStabilityMetricsProvider&) =
      delete;
  ContentStabilityMetricsProvider& operator=(
      const ContentStabilityMetricsProvider&) = delete;
  ~ContentStabilityMetricsProvider() override;

  // MetricsProvider:
  void OnRecordingEnabled() override;
  void OnRecordingDisabled() override;
  void OnPageLoadStarted() override;

 private:
  FRIEND_TEST_ALL_PREFIXES(ContentStabilityMetricsProviderTest,
                           BrowserChildProcessObserverGpu);
  FRIEND_TEST_ALL_PREFIXES(ContentStabilityMetricsProviderTest,
                           BrowserChildProcessObserverUtility);
  FRIEND_TEST_ALL_PREFIXES(ContentStabilityMetricsProviderTest,
                           CdmServiceProcessObserverUtility);
  FRIEND_TEST_ALL_PREFIXES(ContentStabilityMetricsProviderTest,
                           CdmServiceProcessObserverUtilityLaunchFailed);
  FRIEND_TEST_ALL_PREFIXES(ContentStabilityMetricsProviderTest,
                           UnknownCdmServiceProcessObserverUtility);
  FRIEND_TEST_ALL_PREFIXES(ContentStabilityMetricsProviderTest,
                           RenderProcessObserver);
  FRIEND_TEST_ALL_PREFIXES(ContentStabilityMetricsProviderTest,
                           MetricsServicesWebContentObserver);
  FRIEND_TEST_ALL_PREFIXES(ContentStabilityMetricsProviderTest,
                           ExtensionsNotificationObserver);

  // content::RenderProcessHostCreationObserver:
  void OnRenderProcessLaunched(content::RenderProcessHost* host) override;
  void OnRenderProcessHostCreationFailed(
      content::RenderProcessHost* host,
      const content::ChildProcessTerminationInfo& info) override;

  // content::RenderProcessHostObserver:
  void RenderProcessExited(
      content::RenderProcessHost* host,
      const content::ChildProcessTerminationInfo& info) override;
  void RenderProcessHostDestroyed(content::RenderProcessHost* host) override;

  // content::BrowserChildProcessObserver:
  void BrowserChildProcessCrashed(
      const content::ChildProcessData& data,
      const content::ChildProcessTerminationInfo& info) override;
  void BrowserChildProcessLaunchedAndConnected(
      const content::ChildProcessData& data) override;
  void BrowserChildProcessLaunchFailed(
      const content::ChildProcessData& data,
      const content::ChildProcessTerminationInfo& info) override;


  StabilityMetricsHelper helper_;

  base::ScopedMultiSourceObservation<content::RenderProcessHost,
                                     content::RenderProcessHostObserver>
      host_observation_{this};

  std::unique_ptr<ExtensionsHelper> extensions_helper_;
};

}  // namespace metrics

#endif  // COMPONENTS_METRICS_CONTENT_CONTENT_STABILITY_METRICS_PROVIDER_H_
