// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/mojom/gpu_fence_handle_mojom_traits.h"

#include "build/build_config.h"
#include "mojo/public/cpp/system/platform_handle.h"

namespace mojo {

mojo::PlatformHandle
StructTraits<gfx::mojom::GpuFenceHandleDataView,
             gfx::GpuFenceHandle>::native_fd(gfx::GpuFenceHandle& handle) {
  return mojo::PlatformHandle(handle.Release());
}

bool StructTraits<gfx::mojom::GpuFenceHandleDataView, gfx::GpuFenceHandle>::
    Read(gfx::mojom::GpuFenceHandleDataView data, gfx::GpuFenceHandle* out) {
  out->Adopt(data.TakeNativeFd().TakeFD());
  return true;
}

void StructTraits<gfx::mojom::GpuFenceHandleDataView,
                  gfx::GpuFenceHandle>::SetToNull(gfx::GpuFenceHandle* handle) {
#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_WIN)
  handle->Reset();
#endif
}

bool StructTraits<gfx::mojom::GpuFenceHandleDataView,
                  gfx::GpuFenceHandle>::IsNull(const gfx::GpuFenceHandle&
                                                   handle) {
  return handle.is_null();
}

}  // namespace mojo
