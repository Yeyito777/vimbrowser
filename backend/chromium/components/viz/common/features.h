// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_COMMON_FEATURES_H_
#define COMPONENTS_VIZ_COMMON_FEATURES_H_

#include <optional>
#include <string>

#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "build/build_config.h"
#include "components/viz/common/viz_common_export.h"

// See the following for guidance on adding new viz feature flags:
// https://cs.chromium.org/chromium/src/components/viz/README.md#runtime-features

namespace features {

VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(
    kBackForwardTransitionsSameDocSharedImage);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kBackdropFilterMirrorEdgeMode);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kDelegatedCompositing);


VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kDrawQuadSplitLimit);

enum class DelegatedCompositingMode {
  // Enable delegated compositing.
  kFull,
};
extern const VIZ_COMMON_EXPORT base::FeatureParam<DelegatedCompositingMode>
    kDelegatedCompositingModeParam;

VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kUseDrmBlackFullscreenOptimization);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kUseMultipleOverlays);
VIZ_COMMON_EXPORT extern const char kMaxOverlaysParam[];
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kVizFrameSubmissionForWebView);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kWebRtcLogCapturePipeline);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kWebViewVulkanIntermediateBuffer);
#if BUILDFLAG(IS_APPLE)
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kCALayerNewLimit);
VIZ_COMMON_EXPORT extern const base::FeatureParam<int> kCALayerNewLimitDefault;
VIZ_COMMON_EXPORT extern const base::FeatureParam<int>
    kCALayerNewLimitManyVideos;
#endif

#if BUILDFLAG(IS_MAC)
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kVSyncAlignedPresent);
VIZ_COMMON_EXPORT extern const base::FeatureParam<std::string> kTargetForVSync;
VIZ_COMMON_EXPORT extern const char kTargetForVSyncAllFrames[];
VIZ_COMMON_EXPORT extern const char kTargetForVSyncAnimation[];
VIZ_COMMON_EXPORT extern const char kTargetForVSyncInteraction[];
#endif

VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(
    kAckCopyOutputRequestEarlyForViewTransition);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kThrottleFrameSinksOnInteraction);
VIZ_COMMON_EXPORT bool ShouldThrottleWhenInteractiveFrameSinks();
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kAllowUndamagedNonrootRenderPassToSkip);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(
    kAllowForceMergeRenderPassWithRequireOverlayQuads);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kOnBeginFrameThrottleVideo);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kAdpf);
VIZ_COMMON_EXPORT extern const base::FeatureParam<std::string>
    kADPFSocManufacturerAllowlist;
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kEnableADPFRendererMain);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kEnableAdpfEfficiencyMode);
enum class AdpfEfficiencyMode {
  // Never opts ADPF sessions into efficient scheduling (default).
  kNever,
  // Attempt to shift ADPF sessions between setPreferPowerEfficiency states,
  // based on the current context.
  kAdaptive,
  // Always opt ADPF sessions into efficient scheduling whenever possible -
  // costs considerable performance.
  kAlwaysEfficient
};
extern const VIZ_COMMON_EXPORT base::FeatureParam<AdpfEfficiencyMode>
    kAdpfEfficiencyModeParam;
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kEnableADPFSeparateRendererMainSession);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kEnableADPFWorkloadIncreaseOnPageLoad);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kEnableADPFWorkloadReset);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kEnableADPFScrollNoRendererMain);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kEnableADPFBoostRateLimit);
extern const VIZ_COMMON_EXPORT base::FeatureParam<base::TimeDelta>
    kAdpfBoostRateLimitMinWait;
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kEnableADPFAsyncSetThreads);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kEnableADPFIgnoreThrottledTime);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kSelectFutureFrameDeadline);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kUseDisplaySDRMaxLuminanceNits);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kHideDelegatedFrameHostMac);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kEvictionUnlocksResources);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kSingleVideoFrameRateThrottling);

VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kVizDirectCompositorThreadIpcNonRoot);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(
    kVizDirectCompositorThreadIpcFrameSinkManager);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kVizWithIoMessagePump);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kVizNullHypothesis);

VIZ_COMMON_EXPORT extern const char kDraw1Point12Ms[];
VIZ_COMMON_EXPORT extern const char kDraw2Points6Ms[];
VIZ_COMMON_EXPORT extern const char kDraw1Point6Ms[];
VIZ_COMMON_EXPORT extern const char kDraw2Points3Ms[];
VIZ_COMMON_EXPORT extern const char kPredictorKalman[];
VIZ_COMMON_EXPORT extern const char kPredictorLinearResampling[];
VIZ_COMMON_EXPORT extern const char kPredictorLinear1[];
VIZ_COMMON_EXPORT extern const char kPredictorLinear2[];
VIZ_COMMON_EXPORT extern const char kPredictorLsq[];

VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kAckOnSurfaceActivationWhenInteractive);

VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kShutdownForFailedChannelCreation);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kBatchResourceRelease);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kNoLateBeginFrames);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kNoCompositorFrameAcks);
VIZ_COMMON_EXPORT extern const base::FeatureParam<int>
    kNumberPendingFramesUntilThrottle;
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kDisplaySchedulerAsClient);
VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kFlingSchedulingImprovements);

VIZ_COMMON_EXPORT BASE_DECLARE_FEATURE(kRpdqFilterLookupOptimizations);

VIZ_COMMON_EXPORT int DrawQuadSplitLimit();
VIZ_COMMON_EXPORT bool IsRenderPassDrawQuadCullingOptimizationEnabled();
VIZ_COMMON_EXPORT bool IsBackForwardTransitionsSameDocSharedImageEnabled();
VIZ_COMMON_EXPORT bool IsDelegatedCompositingEnabled();
VIZ_COMMON_EXPORT bool IsVizDirectCompositorThreadIpcNonRootEnabled();
VIZ_COMMON_EXPORT bool IsVizDirectCompositorThreadIpcFrameSinkManagerEnabled();
VIZ_COMMON_EXPORT bool IsVizWithIoMessagePumpEnabled();
VIZ_COMMON_EXPORT bool IsUsingVizFrameSubmissionForWebView();
VIZ_COMMON_EXPORT bool IsUsingPreferredIntervalForVideo();
VIZ_COMMON_EXPORT bool ShouldWebRtcLogCapturePipeline();
VIZ_COMMON_EXPORT bool UseWebViewNewInvalidateHeuristic();
VIZ_COMMON_EXPORT bool UseSurfaceLayerForVideo();
VIZ_COMMON_EXPORT int MaxOverlaysConsidered();
VIZ_COMMON_EXPORT bool ShouldOnBeginFrameThrottleVideo();
VIZ_COMMON_EXPORT bool IsVSyncAlignedPresentEnabled();
VIZ_COMMON_EXPORT std::optional<uint64_t>
NumCooldownFramesForAckOnSurfaceActivationDuringInteraction();
VIZ_COMMON_EXPORT extern const base::FeatureParam<int>
    kNumCooldownFramesForAckOnSurfaceActivationDuringInteraction;
VIZ_COMMON_EXPORT bool ShouldAckOnSurfaceActivationWhenInteractive();

VIZ_COMMON_EXPORT bool ShouldAckCOREarlyForViewTransition();

}  // namespace features

#endif  // COMPONENTS_VIZ_COMMON_FEATURES_H_
