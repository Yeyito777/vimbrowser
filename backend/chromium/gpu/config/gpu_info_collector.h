// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_CONFIG_GPU_INFO_COLLECTOR_H_
#define GPU_CONFIG_GPU_INFO_COLLECTOR_H_

#include <stdint.h>

#include "build/build_config.h"
#include "gpu/config/gpu_config_export.h"
#include "gpu/config/gpu_info.h"
#include "gpu/config/gpu_preferences.h"
#include "ui/gfx/gpu_extra_info.h"


namespace gl {
class GLDisplay;
}

namespace angle {
struct SystemInfo;
}

namespace base {
class CommandLine;
}

namespace gpu {
// Collects basic GPU info without creating a GL/DirectX context (and without
// the danger of crashing), including vendor_id and device_id.
// This is called at browser process startup time.
// The subset each platform collects may be different.
GPU_CONFIG_EXPORT bool CollectBasicGraphicsInfo(GPUInfo* gpu_info);

// Similar to above, except it handles the case where the software renderer of
// the platform is used.
GPU_CONFIG_EXPORT bool CollectBasicGraphicsInfo(
    const base::CommandLine* command_line,
    GPUInfo* gpu_info);

// Create a GL/DirectX context and collect related info.
// This is called at GPU process startup time.
GPU_CONFIG_EXPORT bool CollectContextGraphicsInfo(GPUInfo* gpu_info);


// Create a GL context and collect GL strings and versions.
GPU_CONFIG_EXPORT bool CollectGraphicsInfoGL(GPUInfo* gpu_info,
                                             gl::GLDisplay* display);

// If more than one GPUs are identified, and GL strings are available,
// identify the active GPU based on GL strings.
GPU_CONFIG_EXPORT void IdentifyActiveGPU(GPUInfo* gpu_info);

// Helper function to convert data from ANGLE's system info gathering library
// into a GPUInfo
void FillGPUInfoFromSystemInfo(GPUInfo* gpu_info,
                               angle::SystemInfo* system_info);

// On Android, this calls CollectContextGraphicsInfo().
// On other platforms, this calls CollectBasicGraphicsInfo().
GPU_CONFIG_EXPORT void CollectGraphicsInfoForTesting(GPUInfo* gpu_info);

// Collect Graphics info related to the current process
GPU_CONFIG_EXPORT bool CollectGpuExtraInfo(gfx::GpuExtraInfo* gpu_extra_info,
                                           const GpuPreferences& prefs);

// Collect Dawn Toggle name info for about:gpu
GPU_CONFIG_EXPORT void CollectDawnInfo(
    const gpu::GpuPreferences& gpu_preferences,
    bool collect_metrics,
    std::vector<std::string>* dawn_info_list);

}  // namespace gpu

#endif  // GPU_CONFIG_GPU_INFO_COLLECTOR_H_
