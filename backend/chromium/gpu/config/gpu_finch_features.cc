// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/config/gpu_finch_features.h"

#include <string_view>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "build/build_config.h"
#include "gpu/config/gpu_feature_info.h"
#include "gpu/config/gpu_switches.h"
#include "ui/gl/gl_features.h"
#include "ui/gl/gl_surface_egl.h"
#include "ui/gl/gl_switches.h"
#include "ui/gl/gl_utils.h"


#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"
#include "base/system/sys_info.h"
#endif  // BUILDFLAG(IS_MAC)

namespace features {
namespace {


}  // namespace

// More aggressive behavior for the shader cache: increase size, and do not
// purge as much in case of memory pressure.
BASE_FEATURE(kAggressiveShaderCacheLimits, base::FEATURE_DISABLED_BY_DEFAULT);


// When enabled, gives GpuChannel/Host its own dedicated Mojo pipe instead
// of associating with an unused IPC::Channel.
BASE_FEATURE(kRemoveGPULegacyIPC, base::FEATURE_DISABLED_BY_DEFAULT);

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)
// Feature flag to control whether SharedImageStub sequence uses high priority
// on ChromeOS and Linux. Enabled by default.
BASE_FEATURE(kSharedImageStubHighPriority, base::FEATURE_DISABLED_BY_DEFAULT);
#endif

// Enable GPU Rasterization by default. This can still be overridden by
// --enable-gpu-rasterization or --disable-gpu-rasterization.
// DefaultEnableGpuRasterization has launched on Mac, Windows, ChromeOS,
// Android and Linux.
BASE_FEATURE(kDefaultEnableGpuRasterization,
#if BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS) || 0 || BUILDFLAG(IS_FUCHSIA) || BUILDFLAG(IS_LINUX)
             base::FEATURE_ENABLED_BY_DEFAULT
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif
);

// Use a compound backing for shared images by default.
BASE_FEATURE(kUseCompoundImageBackingAsDefault,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables the use of MSAA in skia on Ice Lake and later intel architectures.
BASE_FEATURE(kEnableMSAAOnNewIntelGPUs,
             base::FEATURE_DISABLED_BY_DEFAULT
);

#if BUILDFLAG(IS_WIN)
BASE_FEATURE(kNoUndamagedOverlayPromotion, base::FEATURE_DISABLED_BY_DEFAULT);
#endif

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_IOS)
// If enabled, the TASK_CATEGORY_POLICY value of the GPU process will be
// adjusted to match the one from the browser process every time it changes.
BASE_FEATURE(kAdjustGpuProcessPriority, base::FEATURE_DISABLED_BY_DEFAULT);
#endif

// When enabled, Grshader disk cache will be cleared on startup if any cache
// entry prefix does not match with the current prefix. prefix is made up of
// various parameters like chrome version, driver version etc.
BASE_FEATURE(kClearGrShaderDiskCacheOnInvalidPrefix,
             base::FEATURE_DISABLED_BY_DEFAULT);

// When enabled, Chrome will use the shader disk cache. This feature provides a
// kill-switch for working around issues with the disk cache and assessing the
// performance value of the disk cache. The --disable-gpu-shader-disk-cache flag
// overrides this feature and forces the disk cache to be disabled.
BASE_FEATURE(kGpuShaderDiskCache, base::FEATURE_ENABLED_BY_DEFAULT);

bool IsShaderDiskCacheEnabled(const base::CommandLine* command_line) {
  if (command_line->HasSwitch(switches::kDisableGpuShaderDiskCache)) {
    return false;
  }

  return base::FeatureList::IsEnabled(kGpuShaderDiskCache);
}

// Enable Vulkan graphics backend for compositing and rasterization. Defaults to
// native implementation if --use-vulkan flag is not used. Otherwise
// --use-vulkan will be followed.
// Note Android WebView uses kWebViewDrawFunctorUsesVulkan instead of this.
BASE_FEATURE(kVulkan,
             base::FEATURE_DISABLED_BY_DEFAULT
);

// Force enable WebGPU interop when enabled. When disabled the webgpu interop
// mechanism will default to auto detection in 'GetWebGPUOnVulkanViaGLInterop'
// function.
BASE_FEATURE(kForceEnableWebGpuInterop, base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kEnableDrDc,
#if BUILDFLAG(IS_MAC)
             // DrDC will not be running if Graphite is disabled on Mac.
             base::FEATURE_DISABLED_BY_DEFAULT
#else
             // NOT SUPPORTED. DO NOT ENABLE!
             base::FEATURE_DISABLED_BY_DEFAULT
#endif
);

// Enable WebGPU on gpu service side only. This is used with origin trial and
// enabled by default on supported platforms.
#if BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_CHROMEOS) || 0 || BUILDFLAG(USE_WEBGPU_ON_VULKAN_VIA_GL_INTEROP)
#define WEBGPU_ENABLED base::FEATURE_ENABLED_BY_DEFAULT
#else
#define WEBGPU_ENABLED base::FEATURE_DISABLED_BY_DEFAULT
#endif
BASE_FEATURE(kWebGPUService, WEBGPU_ENABLED);
BASE_FEATURE(kWebGPUBlobCache, WEBGPU_ENABLED);
#undef WEBGPU_ENABLED

// Feature enforces WebGPU security in Android Advanced Protection Mode.
// Disable feature by default for Finch testing.
BASE_FEATURE(kAAPMBlocksWebGPU, base::FEATURE_DISABLED_BY_DEFAULT);

// List of Dawn toggles for WebGPU, delimited by ,
// The FeatureParam may be overridden via Finch config, or via the command line
// For example:
//   --enable-field-trial-config \
//   --force-fieldtrial-params=WebGPU.Enabled:DisabledToggles/toggle1%2Ctoggle2
// Note that the comma should be URL-encoded.
const base::FeatureParam<std::string> kWebGPUDisabledToggles{
    &kWebGPUService, "DisabledToggles", ""};
const base::FeatureParam<std::string> kWebGPUEnabledToggles{
    &kWebGPUService, "EnabledToggles", ""};
// List of WebGPU feature names, delimited by ,
// The FeatureParam may be overridden via Finch config, or via the command line
// For example:
//   --enable-field-trial-config \
//   --force-fieldtrial-params=WebGPU.Enabled:UnsafeFeatures/timestamp-query%2Cshader-f16
// Note that the comma should be URL-encoded.
const base::FeatureParam<std::string> kWebGPUUnsafeFeatures{
    &kWebGPUService, "UnsafeFeatures", ""};
// Whether to enable Dawn's spontaneous wire mode on the server side for faster
// async resolution and timed wait any on the client side.
const base::FeatureParam<bool> kWebGPUSpontaneousWireServer{
    &kWebGPUService, "DawnSpontaneousWireServer", true};
// List of WGSL feature names, delimited by ,
// The FeatureParam may be overridden via Finch config, or via the command line
// For example:
//   --enable-field-trial-config \
//   --force-fieldtrial-params=WebGPU.Enabled:UnsafeWGSLFeatures/feature_1%2Cfeature_2
// Note that the comma should be URL-encoded.
const base::FeatureParam<std::string> kWGSLUnsafeFeatures{
    &kWebGPUService, "UnsafeWGSLFeatures", ""};

BASE_FEATURE(kWebGPUEnableRangeAnalysisForRobustness,
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kWebGPUUseSpirv14, base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kWebGPUDecomposeUniformBuffers, base::FEATURE_ENABLED_BY_DEFAULT);


// Enable Skia Graphite with the platform's default Dawn backend.
// Note: This can be overridden by --enable-skia-graphite and
// --disable-skia-graphite which take precedence over the feature flag, and the
// Dawn backend can be overridden with the --skia-graphite-dawn-backend flag.
BASE_FEATURE(kSkiaGraphite,
#if BUILDFLAG(IS_APPLE)
             base::FEATURE_ENABLED_BY_DEFAULT
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif
);

// Allows CompoundImageBacking to allocate backings during runtime if a
// compatible backing to serve clients requested usage is not already present.
BASE_FEATURE(kUseDynamicBackingAllocations, base::FEATURE_DISABLED_BY_DEFAULT);

// Enable atlasing of small paths on Skia Graphite. Only meaningful if
// SkiaGraphite is also enabled.
BASE_FEATURE(kSkiaGraphiteSmallPathAtlas, base::FEATURE_DISABLED_BY_DEFAULT);

// Enable Skia Graphite's Pipeline precompilation feature.
// Note: This is only meaningful when Skia Graphite is enabled but can then also
// be overridden by
// --enable-skia-graphite-precompilation and
// --disable-skia-graphite-precompilation.
BASE_FEATURE(kSkiaGraphitePrecompilation, base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kConditionallySkipGpuChannelFlush,
// To enable on ChromeOS, test failures must be investigated
// (crrev.com/c/5435673).
#if BUILDFLAG(IS_CHROMEOS)
             base::FEATURE_DISABLED_BY_DEFAULT
#else
             base::FEATURE_ENABLED_BY_DEFAULT
#endif
);

// Whether the Dawn "skip_validation" toggle is enabled for Skia Graphite.
const base::FeatureParam<bool> kSkiaGraphiteDawnSkipValidation{
    &kSkiaGraphite, "dawn_skip_validation", !DCHECK_IS_ON()};

// Whether Dawn backend validation is enabled for Skia Graphite.
const base::FeatureParam<bool> kSkiaGraphiteDawnBackendValidation{
    &kSkiaGraphite, "dawn_backend_validation", false};

// Whether Dawn backend debug labels are enabled for Skia Graphite.
// Only enable backend labels by default on DCHECK builds since it
// can have non-trivial performance overhead e.g. with Metal.
const base::FeatureParam<bool> kSkiaGraphiteDawnBackendDebugLabels{
    &kSkiaGraphite, "dawn_backend_debug_labels", DCHECK_IS_ON()};

// Enables automatic buffer mappings in Dawn's backend.
const base::FeatureParam<bool> kSkiaGraphiteDawnEnableAutoMap{
    &kSkiaGraphite, "dawn_enable_auto_map", true};

// Whether to use PersistentCache for Dawn's pipeline cache.
BASE_FEATURE_PARAM(bool,
                   kSkiaGraphiteDawnUsePersistentCache,
                   &kSkiaGraphite,
                   "dawn_use_persistent_cache",
                   BUILDFLAG(IS_ANDROID));

const base::FeatureParam<int> kSkiaGraphiteMaxPendingRecordings{
    &kSkiaGraphite, "max_pending_recordings", 100};

const base::FeatureParam<int> kSkiaGraphiteMinPathSizeForMsaa{
    &kSkiaGraphiteSmallPathAtlas, "min_path_size_for_msaa", 0};

// Whether to enable deferred submissions optimization (if possible). If it's
// false, every SI's access will require a Graphite's Context::submit() call
// before EndAccess()
BASE_FEATURE_PARAM(bool,
                   kSkiaGraphiteEnableDeferredSubmit,
                   &kSkiaGraphite,
                   "enable_deferred_submit",
                   true);

const base::FeatureParam<bool> kSkiaGraphiteEnableMSAAOnNewerIntel{
    &kSkiaGraphite, "enable_msaa_on_newer_intel", true};

#if BUILDFLAG(IS_WIN)
// Whether the we should DumpWithoutCrashing when D3D related errors are detected.
const base::FeatureParam<bool> kSkiaGraphiteDawnDumpWCOnD3DError{
    &kSkiaGraphite, "dawn_dumpwc_d3d_errors", false};

// Whether to disable D3D shader optimizations.
const base::FeatureParam<bool> kSkiaGraphiteDawnDisableD3DShaderOptimizations{
    &kSkiaGraphite, "dawn_disable_d3d_shader_optimizations", false};

// Whether the Dawn D3D11 flush should be delayed until the end of the frame.
const base::FeatureParam<bool> kSkiaGraphiteDawnD3D11DelayFlush{
    &kSkiaGraphite, "dawn_d3d11_delay_flush", true};

BASE_FEATURE(kSkiaGraphiteDawnUseD3D12, base::FEATURE_DISABLED_BY_DEFAULT);
#endif

// Whether to use the GpuPersistentCache for caching GPU process shader blobs.
// Usage for Graphite is controlled independently with
// kSkiaGraphiteDawnUsePersistentCache.
BASE_FEATURE(kGpuPersistentCache, base::FEATURE_DISABLED_BY_DEFAULT);

// Enabling this will make the GPU decode path use a mock implementation of
// discardable memory.
BASE_FEATURE(kNoDiscardableMemoryForGpuDecodePath,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Use a 100-command limit before forcing context switch per command buffer
// instead of 20.
BASE_FEATURE(kIncreasedCmdBufferParseSlice, base::FEATURE_DISABLED_BY_DEFAULT);

// Prune transfer cache entries not accessed recently. This also turns off
// similar logic in cc::GpuImageDecodeCache which is the largest (often single)
// client of transfer cache.
BASE_FEATURE(kPruneOldTransferCacheEntries, base::FEATURE_DISABLED_BY_DEFAULT);

// On platforms with delegated compositing, try to release overlays later, when
// no new frames are swapped.
BASE_FEATURE(kDeferredOverlaysRelease,
             "DeferredOverlayRelease",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Use d3d11 UpdateSubresource() (instead of a staging texture) to upload pixels
// to textures.
#if BUILDFLAG(IS_WIN)
BASE_FEATURE(kD3DBackingUploadWithUpdateSubresource,
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif

// This feature allows enabling specific entries in
// software_rendering_list.json, via experimentation. The entries must have
// test_group property and test_group feature parameter should be set in the
// experiment for the entries that need to be enabled.
BASE_FEATURE(kGPUBlockListTestGroup, base::FEATURE_DISABLED_BY_DEFAULT);
const base::FeatureParam<int> kGPUBlockListTestGroupId{&kGPUBlockListTestGroup,
                                                       "test_group", 0};

// This feature allows enabling specific entries in gpu_driver_bug_list.json,
// via experimentation. The entries must have test_group property and
// test_group feature parameter should be set in the experiment for the entries
// that need to be enabled.
BASE_FEATURE(kGPUDriverBugListTestGroup, base::FEATURE_DISABLED_BY_DEFAULT);
const base::FeatureParam<int> kGPUDriverBugListTestGroupId{
    &kGPUDriverBugListTestGroup, "test_group", 0};

#if BUILDFLAG(IS_LINUX)
bool IsForceEnableWebGpuInterop() {
  return base::FeatureList::IsEnabled(kForceEnableWebGpuInterop);
}
#endif

bool IsUsingVulkan() {
  return base::FeatureList::IsEnabled(kVulkan);
}

bool IsUsingThreadSafeMediaForWebView() {
  return false;
}

// Note that DrDc is also disabled on some of the gpus (crbug.com/1354201).
// Thread safe media will still be used on those gpus which should be fine for
// now as the lock shouldn't have much overhead and is limited to only few gpus.
// This should be fixed/updated later to account for disabled gpus.
bool NeedThreadSafeAndroidMedia() {
  // If GpuFeatureInfo is available, replace ShouldEnableDrDc() with
  // IsDrDcEnabled(gpu_feature_info) which is set after checking drdc
  // workarounds;
  return ShouldEnableDrDc() || IsUsingThreadSafeMediaForWebView();
}

namespace {
bool IsSkiaGraphiteSupportedByDevice(const base::CommandLine* command_line) {
#if BUILDFLAG(IS_APPLE)
  // Graphite only works well with ANGLE Metal on Mac or iOS.
  // TODO(crbug.com/40063538): Remove this after ANGLE Metal launches fully.
  const bool is_angle_metal_enabled =
      UsePassthroughCommandDecoder() &&
      (base::FeatureList::IsEnabled(features::kDefaultANGLEMetal) ||
       command_line->GetSwitchValueASCII(switches::kUseANGLE) ==
           gl::kANGLEImplementationMetalName);
  if (!is_angle_metal_enabled) {
    return false;
  }
#if BUILDFLAG(IS_MAC)
  // This function only works in the Browser process on Macs. Calling
  // HardwareModelName() from the Renderer or GPU processes will result in an
  // empty hardware model name and an inability to detect unsupported devices.

  // The following code tries to match angle::IsMetalRendererAvailable().
  auto model_name_split = base::SysInfo::SplitHardwareModelNameDoNotUse(
      base::SysInfo::HardwareModelName());
  if (model_name_split.has_value()) {
    // We hardcode the minimum model numbers supporting Mac2 Metal GPU family
    // since ANGLE Metal requires that. We can't check if ANGLE uses Metal until
    // we initialize the GPU process, but this code runs in the browser so we
    // just do our best here to skip the feature check below if we know that
    // ANGLE can't possibly use Metal since we don't want to contaminate the
    // experiment arms with devices that won't run Graphite. Any models not in
    // the list are those that support Mac2 GPU family universally e.g. Mac
    // Mini/Studio. The 5K Retina iMac15,1 is special as it has a discrete GPU
    // and can support ANGLE Metal, but its successors can't until iMac17,1.
    const bool is_imac_15_1 = model_name_split->category == "iMac" &&
                              model_name_split->model == 15 &&
                              model_name_split->variant == 1;
    if (!is_imac_15_1) {
      static constexpr struct {
        std::string category;
        int32_t min_supported_model;
      } kModelSupportData[] = {
          {"MacBookPro", 13}, {"MacBookAir", 8}, {"MacBook", 9},
          {"iMac", 17},       {"iMacPro", 1},    {"Macmini", 8},
      };
      for (const auto& [category, min_supported_model] : kModelSupportData) {
        if (model_name_split->category == category) {
          if (model_name_split->model < min_supported_model) {
            return false;
          }
          break;
        }
      }
    }
  }
#endif  // BUILDFLAG(IS_MAC)
  return true;
#elif BUILDFLAG(IS_CHROMEOS)
  // Graphite on ChromeOS uses the Dawn Vulkan backend. Only enable Graphite if
  // device would already be using Ganesh/Vulkan.
  return IsUsingVulkan();
#elif BUILDFLAG(IS_WIN) && defined(ARCH_CPU_ARM64)
  // Graphite on Windows ARM requires further research.
  return false;
#elif BUILDFLAG(IS_WIN)
  return true;
#else
  // Disallow Graphite from being enabled via the base::Feature on
  // not-yet-supported platforms to avoid users experiencing undefined behavior,
  // including behavior that might prevent them from being able to return to
  // chrome://flags to disable the feature.
  if (base::FeatureList::IsEnabled(features::kSkiaGraphite)) {
    LOG(ERROR) << "Enabling Graphite on a not-yet-supported platform is "
                  "disallowed for safety";
  }
  return false;
#endif
}
}  // namespace

// This function should be called only from the browser process on all platforms
// so that the finch flag check will happen in exactly one place and then the
// Graphite enabled state will be propagated elsewhere via GpuPreferences to GPU
// process launch and then later to renderer processes via GpuFeatureInfo.

bool IsSkiaGraphiteEnabled(const base::CommandLine* command_line) {
  // Force disabling graphite if --disable-skia-graphite flag is specified.
  if (command_line->HasSwitch(switches::kDisableSkiaGraphite)) {
    return false;
  }

  // Force Graphite on if --enable-skia-graphite flag is specified.
  if (command_line->HasSwitch(switches::kEnableSkiaGraphite)) {
    return true;
  }

  if (!IsSkiaGraphiteSupportedByDevice(command_line)) {
    // Return early before checking "SkiaGraphite" feature so that devices
    // which don't support graphite are not included in the finch study.
    return false;
  }

  return base::FeatureList::IsEnabled(features::kSkiaGraphite);
}

bool IsDrDcEnabled(const gpu::GpuFeatureInfo& gpu_feature_info) {
  return gpu_feature_info.status_values
             [gpu::GPU_FEATURE_TYPE_DIRECT_RENDERING_DISPLAY_COMPOSITOR] ==
         gpu::kGpuFeatureStatusEnabled;
}

bool ShouldEnableDrDc() {

  return base::FeatureList::IsEnabled(kEnableDrDc);
}

bool IsSkiaGraphitePrecompilationEnabled(
    const base::CommandLine* command_line) {
  if (!IsSkiaGraphiteEnabled(command_line)) {
    return false;
  }

  // Force disabling Graphite Precompilation if
  // --disable-skia-graphite-precompilation flag is specified.
  if (command_line->HasSwitch(switches::kDisableSkiaGraphitePrecompilation)) {
    return false;
  }

  // Force Graphite Precompilation on if --enable-skia-graphite-precompilation
  // flag is specified.
  if (command_line->HasSwitch(switches::kEnableSkiaGraphitePrecompilation)) {
    return true;
  }

  return base::FeatureList::IsEnabled(features::kSkiaGraphitePrecompilation);
}

// Set up such that service side purge depends on the client side purge feature
// being enabled. And enabling service side purge disables client purge
bool EnablePurgeGpuImageDecodeCache() {
  return !base::FeatureList::IsEnabled(kPruneOldTransferCacheEntries);
}
bool EnablePruneOldTransferCacheEntries() {
  return base::FeatureList::IsEnabled(kPruneOldTransferCacheEntries);
}

bool IsLegacyIpcDisabled() {
  return base::FeatureList::IsEnabled(kRemoveGPULegacyIPC);
}


// When this flag is enabled, stops using gpu::SyncPointOrderData for sync point
// validation, uses gpu::TaskGraph instead.
// Graph-based validation doesn't require sync point releases are submitted to
// the scheduler prior to their corresponding waits. Therefore it allows to
// remove the synchronous flush done by VerifySyncTokens().
//
// TODO(b/324276400): Work in progress.
BASE_FEATURE(kSyncPointGraphValidation, base::FEATURE_DISABLED_BY_DEFAULT);

bool IsSyncPointGraphValidationEnabled() {
  return base::FeatureList::IsEnabled(kSyncPointGraphValidation);
}

BASE_FEATURE(kANGLEPerContextBlobCache, base::FEATURE_DISABLED_BY_DEFAULT);

#if BUILDFLAG(IS_APPLE)
BASE_FEATURE(kIOSurfaceMultiThreading, base::FEATURE_ENABLED_BY_DEFAULT);
#endif

// Support thread safety for graphite::context by sharing the same
// graphite::context as well as its wrapper class GraphiteSharedContext between
// GpuMain and CompositorGpuThread. Note: When this feature is disabled,
// each thread creates its own graphite::context and the context wrapper.
BASE_FEATURE(kGraphiteContextIsThreadSafe,
#if BUILDFLAG(IS_MAC)
             // DrDC needs a thread-safe graphite context to work correctly.
             base::FEATURE_ENABLED_BY_DEFAULT);
#else
             // Feature incomplete. DO NOT ENABLE!
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif

bool IsGraphiteContextThreadSafe() {
  return base::FeatureList::IsEnabled(features::kGraphiteContextIsThreadSafe);
}

BASE_FEATURE(kWebGPUCompatibilityMode, base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kWebGPUAndroidOpenGLES, base::FEATURE_ENABLED_BY_DEFAULT);

#if BUILDFLAG(IS_WIN)
BASE_FEATURE(kWebGPUQualcommWindows, base::FEATURE_ENABLED_BY_DEFAULT);
#endif

// Enables runtime configuration of the GPU watchdog timeout via
// experimentation.
BASE_FEATURE(kConfigurableGPUWatchdogTimeout,
             base::FEATURE_DISABLED_BY_DEFAULT);
const base::FeatureParam<int> kConfigurableGPUWatchdogTimeoutSeconds{
    &kConfigurableGPUWatchdogTimeout, "watchdog_timeout_seconds", 30};
}  // namespace features
