/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "src/gpu/ganesh/GrCanvas.h"

#include "src/core/SkCanvasPriv.h"
#include "src/core/SkDevice.h"
#include "src/gpu/ganesh/Device.h"
#include "src/gpu/ganesh/GrRenderTargetProxy.h"

namespace skgpu::ganesh {

SurfaceDrawContext* TopDeviceSurfaceDrawContext(const SkCanvas* canvas) {
    if (auto gpuDevice = SkCanvasPriv::TopDevice(canvas)->asGaneshDevice()) {
        return gpuDevice->surfaceDrawContext();
    }
    return nullptr;
}

SurfaceFillContext* TopDeviceSurfaceFillContext(const SkCanvas* canvas) {
    if (auto gpuDevice = SkCanvasPriv::TopDevice(canvas)->asGaneshDevice()) {
        return gpuDevice->surfaceFillContext();
    }
    return nullptr;
}

GrRenderTargetProxy* TopDeviceTargetProxy(const SkCanvas* canvas) {
    if (auto gpuDevice = SkCanvasPriv::TopDevice(canvas)->asGaneshDevice()) {
        return gpuDevice->targetProxy();
    }
    return nullptr;
}

}  // namespace skgpu::ganesh
