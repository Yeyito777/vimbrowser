// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_GPU_MEMORY_BUFFER_HANDLE_H_
#define UI_GFX_GPU_MEMORY_BUFFER_HANDLE_H_

#include <stddef.h>
#include <stdint.h>

#include <utility>

#include "base/check_op.h"
#include "base/component_export.h"
#include "base/memory/unsafe_shared_memory_region.h"
#include "base/notreached.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_OZONE)
#include "ui/gfx/native_pixmap_handle.h"
#elif BUILDFLAG(IS_APPLE)
#include "ui/gfx/mac/io_surface.h"
#endif

namespace mojo {
template <typename, typename>
struct StructTraits;
template <typename, typename>
struct UnionTraits;
}  // namespace mojo

namespace gfx {
namespace mojom {
class DXGIHandleDataView;
class GpuMemoryBufferPlatformHandleDataView;
}  // namespace mojom

enum GpuMemoryBufferType {
  EMPTY_BUFFER,
  SHARED_MEMORY_BUFFER,
#if BUILDFLAG(IS_APPLE)
  IO_SURFACE_BUFFER,
#elif BUILDFLAG(IS_OZONE)
  NATIVE_PIXMAP,
#endif
};


// TODO(crbug.com/40584691): Convert this to a proper class to ensure the state
// is always consistent, particularly that the only one handle is set at the
// same time and it corresponds to |type|.
struct COMPONENT_EXPORT(GFX) GpuMemoryBufferHandle {
  GpuMemoryBufferHandle();
  explicit GpuMemoryBufferHandle(base::UnsafeSharedMemoryRegion region);
#if BUILDFLAG(IS_OZONE)
  explicit GpuMemoryBufferHandle(gfx::NativePixmapHandle native_pixmap_handle);
#elif BUILDFLAG(IS_APPLE)
  explicit GpuMemoryBufferHandle(ScopedIOSurface io_surface);
#endif
  GpuMemoryBufferHandle(GpuMemoryBufferHandle&& other);
  GpuMemoryBufferHandle& operator=(GpuMemoryBufferHandle&& other);
  ~GpuMemoryBufferHandle();

  GpuMemoryBufferHandle Clone() const;
  bool is_null() const { return type == EMPTY_BUFFER; }

  // The shared memory region may only be used with SHARED_MEMORY_BUFFER and
  // DXGI_SHARED_HANDLE. In the case of DXGI handles, the actual contents of the
  // buffer can only be accessed from the GPU process, so `Map()`ing the buffer
  // into memory actually requires an IPC to the GPU process, which then copies
  // the contents into the shmem region so it can be accessed from other
  // processes.
  const base::UnsafeSharedMemoryRegion& region() const& {
    // For now, allow this to transparently forward as appropriate for DXGI or
    // shmem handles in production. However, this will be a hard CHECK() in the
    // future.
    CHECK_EQ(type, SHARED_MEMORY_BUFFER, base::NotFatalUntil::M138);
    switch (type) {
      case SHARED_MEMORY_BUFFER:
        return region_;
      default:
        NOTREACHED();
    }
  }
  base::UnsafeSharedMemoryRegion region() && {
    CHECK_EQ(type, SHARED_MEMORY_BUFFER);
    type = EMPTY_BUFFER;
    return std::move(region_);
  }

#if BUILDFLAG(IS_OZONE)
  const NativePixmapHandle& native_pixmap_handle() const& {
    CHECK_EQ(type, NATIVE_PIXMAP);
    return native_pixmap_handle_;
  }
  NativePixmapHandle native_pixmap_handle() && {
    CHECK_EQ(type, NATIVE_PIXMAP);
    type = EMPTY_BUFFER;
    return std::move(native_pixmap_handle_);
  }
#endif  // BUILDFLAG(IS_OZONE)


#if BUILDFLAG(IS_APPLE)
  const ScopedIOSurface& io_surface() const& { return io_surface_; }
  ScopedIOSurface io_surface() && {
    CHECK_EQ(type, IO_SURFACE_BUFFER);
    type = EMPTY_BUFFER;
    return std::move(io_surface_);
  }
#endif  // BUILDFLAG(IS_APPLE)

  GpuMemoryBufferType type = GpuMemoryBufferType::EMPTY_BUFFER;

  uint32_t offset = 0;
  uint32_t stride = 0;


 private:
  friend mojo::UnionTraits<mojom::GpuMemoryBufferPlatformHandleDataView,
                           GpuMemoryBufferHandle>;

  // This naming isn't entirely styleguide-compliant, but per the TODO, the end
  // goal is to make `this` an encapsulated class.
  base::UnsafeSharedMemoryRegion region_;

#if BUILDFLAG(IS_OZONE)
  NativePixmapHandle native_pixmap_handle_;
#endif  // BUILDFLAG(IS_OZONE)


#if BUILDFLAG(IS_APPLE)
  ScopedIOSurface io_surface_;
#endif  // BUILDFLAG(IS_APPLE)
};

}  // namespace gfx

#endif  // UI_GFX_GPU_MEMORY_BUFFER_HANDLE_H_
