// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/common/features.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "base/metrics/field_trial_params.h"
#include "base/strings/string_split.h"
#include "base/system/sys_info.h"
#include "build/build_config.h"
#include "components/viz/common/switches.h"
#include "components/viz/common/viz_utils.h"
#include "gpu/config/gpu_driver_bug_workaround_type.h"
#include "gpu/config/gpu_finch_features.h"
#include "gpu/config/gpu_switches.h"
#include "media/media_buildflags.h"
#include "ui/gl/gl_switches.h"



namespace features {


// When there is a screenshot request against a surface, issue the copy request
// into a shared image.
BASE_FEATURE(kBackForwardTransitionsSameDocSharedImage,
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kBackdropFilterMirrorEdgeMode, base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kUseDrmBlackFullscreenOptimization,
             base::FEATURE_DISABLED_BY_DEFAULT
);


BASE_FEATURE(kUseMultipleOverlays,
             base::FEATURE_DISABLED_BY_DEFAULT
);
const char kMaxOverlaysParam[] = "max_overlays";

BASE_FEATURE(kDelegatedCompositing, base::FEATURE_DISABLED_BY_DEFAULT);

const char kDrawQuadSplit[] = "num_of_splits";

// If enabled, overrides the maximum number (exclusive) of quads one draw quad
// can be split into during occlusion culling.
BASE_FEATURE(kDrawQuadSplitLimit, base::FEATURE_DISABLED_BY_DEFAULT);

constexpr base::FeatureParam<DelegatedCompositingMode>::Option
    kDelegatedCompositingModeOption[] = {
        {DelegatedCompositingMode::kFull, "full"},
};
const base::FeatureParam<DelegatedCompositingMode>
    kDelegatedCompositingModeParam = {
        &kDelegatedCompositing,
        "mode",
        DelegatedCompositingMode::kFull,
        &kDelegatedCompositingModeOption,
};


// Submit CompositorFrame from SynchronousLayerTreeFrameSink directly to viz in
// WebView.
BASE_FEATURE(kVizFrameSubmissionForWebView, base::FEATURE_DISABLED_BY_DEFAULT);


// Whether we should log extra debug information to webrtc native log.
BASE_FEATURE(kWebRtcLogCapturePipeline, base::FEATURE_DISABLED_BY_DEFAULT);

// Used to debug Android WebView Vulkan composite. Composite to an intermediate
// buffer and draw the intermediate buffer to the secondary command buffer.
BASE_FEATURE(kWebViewVulkanIntermediateBuffer,
             base::FEATURE_DISABLED_BY_DEFAULT);


#if BUILDFLAG(IS_APPLE)
// Increase the max CALayer number allowed for CoreAnimation.
// * If this feature is disabled, then the default limit is 128 quads,
//   unless there are 5 or more video elements present, in which case
//   the limit is 300.
// * If this feature is enabled, then these limits are 512, and can be
// overridden by the "default" and "many-videos"
//   feature parameters.
BASE_FEATURE(kCALayerNewLimit, base::FEATURE_DISABLED_BY_DEFAULT);
// Set FeatureParam default to -1. CALayerOverlayProcessor choose the default in
// ca_layer_overlay.cc When it's < 0.
const base::FeatureParam<int> kCALayerNewLimitDefault{&kCALayerNewLimit,
                                                      "default", -1};
const base::FeatureParam<int> kCALayerNewLimitManyVideos{&kCALayerNewLimit,
                                                         "many-videos", -1};
#endif

#if BUILDFLAG(IS_MAC)
// Whether the presentation should be delayed until the next DisplayLink
// callback. Currently only for frames that handle interaction.
BASE_FEATURE(kVSyncAlignedPresent, base::FEATURE_ENABLED_BY_DEFAULT);
#endif

// Sends a CopyOutputRequest completion Ack early for view transitions so it can
// proceed with navigation. ViewTransition Animate still waits though for
// CopyOutputRequests to be actually fulfilled.
BASE_FEATURE(kAckCopyOutputRequestEarlyForViewTransition,
             base::FEATURE_ENABLED_BY_DEFAULT);

// If enabled, other frame sinks are throttled when a frame sink is handling
// user interaction.
BASE_FEATURE(kThrottleFrameSinksOnInteraction,
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kAllowUndamagedNonrootRenderPassToSkip,
#if BUILDFLAG(IS_MAC)
             base::FEATURE_ENABLED_BY_DEFAULT);
#else
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif

// Allow SurfaceAggregator to merge render passes when they contain quads that
// require overlay (e.g. protected video). See usage in |EmitSurfaceContent|.
BASE_FEATURE(kAllowForceMergeRenderPassWithRequireOverlayQuads,
             base::FEATURE_ENABLED_BY_DEFAULT);

// if enabled, Any CompositorFrameSink of type video that defines a preferred
// framerate that is below the display framerate will throttle OnBeginFrame
// callbacks to match the preferred framerate.
BASE_FEATURE(kOnBeginFrameThrottleVideo, base::FEATURE_ENABLED_BY_DEFAULT);

// If enabled, Chrome uses ADPF(Android Dynamic Performance Framework) if the
// device's SOC manufacturer is in the allowlist.
// If disabled, Chrome never uses ADPF.
BASE_FEATURE(kAdpf, base::FEATURE_ENABLED_BY_DEFAULT);

const base::FeatureParam<std::string> kADPFSocManufacturerAllowlist{
    &kAdpf, "soc_manufacturer_allowlist", "Google"};

// If enabled, Chrome includes the Renderer Main thread(s) into the
// ADPF(Android Dynamic Performance Framework) hint session.
BASE_FEATURE(kEnableADPFRendererMain, base::FEATURE_ENABLED_BY_DEFAULT);

// If enabled, Chrome puts Renderer Main threads into a separate
// ADPF(Android Dynamic Performance Framework) hint session, and does not
// report any timing hints from this session.
BASE_FEATURE(kEnableADPFSeparateRendererMainSession,
             base::FEATURE_DISABLED_BY_DEFAULT);

// If enabled, Chrome uses notifyWorkloadIncrease ADPF(Android Dynamic
// Performance Framework) method before CrRendererMain starts running a heavy
// workload during page load.
// Supported only on Android >= 16.
BASE_FEATURE(kEnableADPFWorkloadIncreaseOnPageLoad,
             base::FEATURE_DISABLED_BY_DEFAULT);

// If enabled, Chrome uses ADPF's setPreferPowerEfficiency API to try and save
// energy at the cost of performance. Supported only on Android >= 16.
BASE_FEATURE(kEnableAdpfEfficiencyMode, base::FEATURE_DISABLED_BY_DEFAULT);
constexpr base::FeatureParam<AdpfEfficiencyMode>::Option
    kAdpfEfficiencyModeOption[] = {
        // ADPF sessions are always configured for performance.
        {AdpfEfficiencyMode::kNever, "never"},
        // ADPF sessions switch between performance and efficiency mode based on
        // context. TODO(crbug.com/464505581): implement this.
        {AdpfEfficiencyMode::kAdaptive, "adaptive"},
        // ADPF sessions are always configured for efficiency.
        {AdpfEfficiencyMode::kAlwaysEfficient, "always_efficient"}};
const base::FeatureParam<AdpfEfficiencyMode> kAdpfEfficiencyModeParam{
    &kEnableAdpfEfficiencyMode,
    "mode",
    AdpfEfficiencyMode::kNever,
    &kAdpfEfficiencyModeOption,
};

// If enabled, Chrome uses notifyWorkloadReset method on viz wakeup instead of
// sending a timing report with a fake actual duration > target duration.
// Supported only on Android >= 16.
BASE_FEATURE(kEnableADPFWorkloadReset, base::FEATURE_DISABLED_BY_DEFAULT);

// If enabled, Chrome excludes Renderer Main threads from the
// ADPF(Android Dynamic Performance Framework) hint session during scrolls.
// TODO(https://crbug.com/466116123): If this causes scroll regressions,
// experiment with keeping CrRendererMain in the session for main thread
// scrolls.
BASE_FEATURE(kEnableADPFScrollNoRendererMain,
             base::FEATURE_DISABLED_BY_DEFAULT);

// If enabled, Chrome sends an ADPF(Android Dynamic Performance Framework)
// timing report with a fake actual durarion > target duration only if there
// were no frame timing reports for `adpf_boost_rate_limit_min_wait`, instead
// of doing it for every touch start input.
// The goal is to avoid boosts during continuous user input to reduce power
// consumption.
BASE_FEATURE(kEnableADPFBoostRateLimit, base::FEATURE_DISABLED_BY_DEFAULT);

const base::FeatureParam<base::TimeDelta> kAdpfBoostRateLimitMinWait{
    &kEnableADPFBoostRateLimit, "adpf_boost_rate_limit_min_wait",
    base::Milliseconds(50)};

// If enabled, Chrome calls the SetThreads
// ADPF(Android Dynamic Performance Framework) method on a worker thread
// instead of Viz. The goal is to prevent Viz from blocking on a binder call.
BASE_FEATURE(kEnableADPFAsyncSetThreads, base::FEATURE_DISABLED_BY_DEFAULT);

// If enabled, Chrome ignores the time spent between swap throttled and the
// next ScheduleBeginFrameDeadline when sending an ADPF(Android Dynamic
// Performance Framework) timing report.
BASE_FEATURE(kEnableADPFIgnoreThrottledTime, base::FEATURE_DISABLED_BY_DEFAULT);

// If enabled, we immediately send acks to clients when a viz surface
// activates. This effectively removes back-pressure. This can result in wasted
// work and contention, but should regularize the timing of client rendering.
BASE_FEATURE(kAckOnSurfaceActivationWhenInteractive,
             base::FEATURE_ENABLED_BY_DEFAULT);

const base::FeatureParam<int>
    kNumCooldownFramesForAckOnSurfaceActivationDuringInteraction{
        &kAckOnSurfaceActivationWhenInteractive, "frames", 3};

// If enabled, DisplayScheduler will attempt to select a future deadline if the
// preferred deadline is not achievable.
BASE_FEATURE(kSelectFutureFrameDeadline, base::FEATURE_DISABLED_BY_DEFAULT);

// When enabled, SDR maximum luminance nits of then current display will be used
// as the HDR metadata NDWL nits for PQ content (if none was specified). This
// has the effect that its "opts-out" PQ content from being affected by the OS'
// SDR white level (effectively the OS' brightness setting). This behavior is
// preferred on Windows, to avoid flicker when entering/leaving overlays
// (https://crbug.com/40285630) but otherwise is undesirable behavior
// (https://crbug.com/40266959 and https://crbug.com/486121442).
BASE_FEATURE(kUseDisplaySDRMaxLuminanceNits, base::FEATURE_DISABLED_BY_DEFAULT);

// On mac, when the RenderWidgetHostViewMac is hidden, also hide the
// DelegatedFrameHost. Among other things, it unlocks the compositor frames,
// which can saves hundreds of MiB of memory with bfcache entries.
BASE_FEATURE(kHideDelegatedFrameHostMac, base::FEATURE_DISABLED_BY_DEFAULT);

// When enabled, ClientResourceProvider will attempt to unlock and delete
// TransferableResources that have been returned as a part of eviction.
//
// Enabled by default 03/2014, kept to run a holdback experiment.
BASE_FEATURE(kEvictionUnlocksResources, base::FEATURE_DISABLED_BY_DEFAULT);

// If enabled, FrameRateDecider will toggle to half framerate if there's only
// one video on screen whose framerate is lower than the display vsync and in
// perfect cadence.
BASE_FEATURE(kSingleVideoFrameRateThrottling,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Remove gpu process reference if gpu context is loss, and gpu channel cannot
// be established due to said gpu process exiting.
BASE_FEATURE(kShutdownForFailedChannelCreation,
             base::FEATURE_ENABLED_BY_DEFAULT);

// When enabled, ClientResourceProvider will allow for the batching of
// callbacks. So that the client can perform a series of individual releases,
// but have ClientResourceProvider coordinate the callbacks. This allows all of
// the Main-thread callbacks to be batched into a single jump to that thread.
//
// When disabled each callback will perform its own separate post task.
BASE_FEATURE(kBatchResourceRelease, base::FEATURE_DISABLED_BY_DEFAULT);

// When enabled, BeginFrameSource will not send a `BeginFrameArgs::MISSED` in
// response to `AddObserver`. As these consistently miss deadlines, and increase
// latency and jank. Instead the client will receive the next BeginFrame.
BASE_FEATURE(kNoLateBeginFrames, base::FEATURE_DISABLED_BY_DEFAULT);

// Enables IPCs to directly target Viz's compositor thread for non-root
// CompositorFrameSink messages without hopping through the IO thread first.
BASE_FEATURE(kVizDirectCompositorThreadIpcNonRoot,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enables IPCs to directly target Viz's compositor thread for FrameSinkManager
// messages and, in turn, all interfaces associated with it e.g. root compositor
// frame sink, display private - skipping the IO thread hop.
BASE_FEATURE(kVizDirectCompositorThreadIpcFrameSinkManager,
             base::FEATURE_DISABLED_BY_DEFAULT
);

// Switches the message pump to base::MessagePumpType::IO on the Viz thread.
BASE_FEATURE(kVizWithIoMessagePump, base::FEATURE_DISABLED_BY_DEFAULT);

// Null Hypothesis test for viz. This will be used in an meta experiment to
// judge finch variation.
BASE_FEATURE(kVizNullHypothesis, base::FEATURE_DISABLED_BY_DEFAULT);


BASE_FEATURE(kNoCompositorFrameAcks, base::FEATURE_DISABLED_BY_DEFAULT);
const base::FeatureParam<int> kNumberPendingFramesUntilThrottle{
    &kNoCompositorFrameAcks, "pending_frames", 1};
BASE_FEATURE(kDisplaySchedulerAsClient, base::FEATURE_DISABLED_BY_DEFAULT);

// Enables prioritization of the BeginFrame InputClient (like
// FlingSchedulerAndroid) so it can dispatch events before the renderer
// receives its BeginFrame.
BASE_FEATURE(kFlingSchedulingImprovements, base::FEATURE_DISABLED_BY_DEFAULT);

// Enables optimizations in `DirectRenderer` and `OcclusionCuller` that reuses
// pre-existing loops to access filter data from `AggregatedRenderPassDrawQuad`.
// This is a temporary flag to work as a kill switch for the optimization and
// should be removed as soon as we confirm that the optimization is stable.
BASE_FEATURE(kRpdqFilterLookupOptimizations, base::FEATURE_ENABLED_BY_DEFAULT);


int DrawQuadSplitLimit() {
  constexpr int kDefaultDrawQuadSplitLimit = 5;
  constexpr int kMinDrawQuadSplitLimit = 1;
  constexpr int kMaxDrawQuadSplitLimit = 15;

  const int split_limit = base::GetFieldTrialParamByFeatureAsInt(
      kDrawQuadSplitLimit, kDrawQuadSplit, kDefaultDrawQuadSplitLimit);
  return std::clamp(split_limit, kMinDrawQuadSplitLimit,
                    kMaxDrawQuadSplitLimit);
}

bool IsBackForwardTransitionsSameDocSharedImageEnabled() {
  return base::FeatureList::IsEnabled(
      kBackForwardTransitionsSameDocSharedImage);
}

bool IsDelegatedCompositingEnabled() {
  return base::FeatureList::IsEnabled(kDelegatedCompositing);
}

bool IsVizDirectCompositorThreadIpcNonRootEnabled() {
  return base::FeatureList::IsEnabled(kVizDirectCompositorThreadIpcNonRoot);
}

bool IsVizDirectCompositorThreadIpcFrameSinkManagerEnabled() {
  return base::FeatureList::IsEnabled(
      kVizDirectCompositorThreadIpcFrameSinkManager);
}

bool IsVizWithIoMessagePumpEnabled() {
  return base::FeatureList::IsEnabled(kVizWithIoMessagePump);
}

bool IsUsingVizFrameSubmissionForWebView() {
  return base::FeatureList::IsEnabled(kVizFrameSubmissionForWebView);
}

bool ShouldWebRtcLogCapturePipeline() {
  return base::FeatureList::IsEnabled(kWebRtcLogCapturePipeline);
}


bool UseSurfaceLayerForVideo() {
  return true;
}

int MaxOverlaysConsidered() {
  if (!base::FeatureList::IsEnabled(kUseMultipleOverlays)) {
    return 1;
  }

  return base::GetFieldTrialParamByFeatureAsInt(kUseMultipleOverlays,
                                                kMaxOverlaysParam, 8);
}

bool ShouldOnBeginFrameThrottleVideo() {
  return base::FeatureList::IsEnabled(features::kOnBeginFrameThrottleVideo);
}

bool ShouldThrottleWhenInteractiveFrameSinks() {
  return base::FeatureList::IsEnabled(
      features::kThrottleFrameSinksOnInteraction);
}

bool ShouldAckOnSurfaceActivationWhenInteractive() {
  return base::FeatureList::IsEnabled(
      features::kAckOnSurfaceActivationWhenInteractive);
}

std::optional<uint64_t>
NumCooldownFramesForAckOnSurfaceActivationDuringInteraction() {
  if (!ShouldAckOnSurfaceActivationWhenInteractive()) {
    return std::nullopt;
  }
  CHECK_GE(kNumCooldownFramesForAckOnSurfaceActivationDuringInteraction.Get(),
           0)
      << "The number of cooldown frames must be non-negative";
  return static_cast<uint64_t>(
      kNumCooldownFramesForAckOnSurfaceActivationDuringInteraction.Get());
}

#if BUILDFLAG(IS_MAC)
bool IsVSyncAlignedPresentEnabled() {
  return base::FeatureList::IsEnabled(features::kVSyncAlignedPresent);
}
#endif




bool ShouldAckCOREarlyForViewTransition() {
  return base::FeatureList::IsEnabled(
      features::kAckCopyOutputRequestEarlyForViewTransition);
}

}  // namespace features
