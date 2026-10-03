// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/gpu_memory_buffer_handle.h"

#include "base/logging.h"
#include "base/notimplemented.h"
#include "build/build_config.h"
#include "ui/gfx/generic_shared_memory_id.h"


namespace gfx {


GpuMemoryBufferHandle::GpuMemoryBufferHandle() = default;

GpuMemoryBufferHandle::GpuMemoryBufferHandle(
    base::UnsafeSharedMemoryRegion region)
    : type(GpuMemoryBufferType::SHARED_MEMORY_BUFFER),
      region_(std::move(region)) {
  CHECK(region_.IsValid(), base::NotFatalUntil::M155);
}


#if BUILDFLAG(IS_OZONE)
GpuMemoryBufferHandle::GpuMemoryBufferHandle(
    NativePixmapHandle native_pixmap_handle)
    : type(GpuMemoryBufferType::NATIVE_PIXMAP),
      native_pixmap_handle_(std::move(native_pixmap_handle)) {}
#endif  // BUILDFLAG(IS_OZONE)


#if BUILDFLAG(IS_APPLE)
GpuMemoryBufferHandle::GpuMemoryBufferHandle(ScopedIOSurface io_surface)
    : type(GpuMemoryBufferType::IO_SURFACE_BUFFER),
      io_surface_(std::move(io_surface)) {
  CHECK(io_surface_);
}
#endif  // BUILDFLAG(IS_APPLE)

// TODO(crbug.com/40584691): Reset |type| and possibly the handles on the
// moved-from object.
GpuMemoryBufferHandle::GpuMemoryBufferHandle(GpuMemoryBufferHandle&& other) =
    default;

GpuMemoryBufferHandle& GpuMemoryBufferHandle::operator=(
    GpuMemoryBufferHandle&& other) = default;

GpuMemoryBufferHandle::~GpuMemoryBufferHandle() = default;

GpuMemoryBufferHandle GpuMemoryBufferHandle::Clone() const {
  GpuMemoryBufferHandle handle;
  handle.type = type;
  handle.offset = offset;
  handle.stride = stride;
#if BUILDFLAG(IS_OZONE)
  handle.native_pixmap_handle_ = CloneHandleForIPC(native_pixmap_handle_);
#elif BUILDFLAG(IS_APPLE)
  handle.io_surface_ = io_surface_;
#endif
  handle.region_ = region_.Duplicate();
  return handle;
}

}  // namespace gfx
