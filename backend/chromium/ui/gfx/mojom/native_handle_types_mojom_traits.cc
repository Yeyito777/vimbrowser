// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/mojom/native_handle_types_mojom_traits.h"

#include "base/notreached.h"
#include "build/build_config.h"
#include "mojo/public/cpp/base/shared_memory_mojom_traits.h"
#include "mojo/public/cpp/platform/platform_handle.h"

#if BUILDFLAG(IS_APPLE)
#include "base/apple/scoped_mach_port.h"
#include "ui/gfx/mac/io_surface.h"
#endif  // BUILDFLAG(IS_APPLE)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OZONE)
#include "ui/gfx/native_pixmap_handle.h"
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OZONE)



namespace mojo {


#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OZONE)
mojo::PlatformHandle StructTraits<
    gfx::mojom::NativePixmapPlaneDataView,
    gfx::NativePixmapPlane>::buffer_handle(gfx::NativePixmapPlane& plane) {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  return mojo::PlatformHandle(std::move(plane.fd));
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
}

bool StructTraits<
    gfx::mojom::NativePixmapPlaneDataView,
    gfx::NativePixmapPlane>::Read(gfx::mojom::NativePixmapPlaneDataView data,
                                  gfx::NativePixmapPlane* out) {
  out->stride = data.stride();
  out->offset = data.offset();
  out->size = data.size();

  mojo::PlatformHandle handle = data.TakeBufferHandle();
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  if (!handle.is_fd())
    return false;
  out->fd = handle.TakeFD();
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)

  return true;
}


bool StructTraits<
    gfx::mojom::NativePixmapHandleDataView,
    gfx::NativePixmapHandle>::Read(gfx::mojom::NativePixmapHandleDataView data,
                                   gfx::NativePixmapHandle* out) {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  out->modifier = data.modifier();
  out->supports_zero_copy_webgpu_import =
      data.supports_zero_copy_webgpu_import();
#endif


  return data.ReadPlanes(&out->planes);
}
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OZONE)


#if BUILDFLAG(IS_APPLE)
IOSurfaceHandle::IOSurfaceHandle() = default;
IOSurfaceHandle::IOSurfaceHandle(IOSurfaceHandle&&) = default;
IOSurfaceHandle& IOSurfaceHandle::operator=(IOSurfaceHandle&&) = default;
IOSurfaceHandle::~IOSurfaceHandle() = default;

bool StructTraits<gfx::mojom::IOSurfaceHandleDataView, IOSurfaceHandle>::Read(
    gfx::mojom::IOSurfaceHandleDataView data,
    IOSurfaceHandle* handle) {
  handle->mach_send_right = data.TakeMachSendRight().TakeMachSendRight();
  if (!handle->mach_send_right.is_valid()) {
    return false;
  }
  return true;
}
#endif  // BUILDFLAG(IS_APPLE)

gfx::mojom::GpuMemoryBufferPlatformHandleDataView::Tag UnionTraits<
    gfx::mojom::GpuMemoryBufferPlatformHandleDataView,
    gfx::GpuMemoryBufferHandle>::GetTag(const gfx::GpuMemoryBufferHandle&
                                            handle) {
  switch (handle.type) {
    case gfx::EMPTY_BUFFER:
      NOTREACHED();  // Handled by `IsNull()` and should never reach here.
    case gfx::SHARED_MEMORY_BUFFER:
      return Tag::kSharedMemoryHandle;
#if BUILDFLAG(IS_APPLE)
    case gfx::IO_SURFACE_BUFFER:
      return Tag::kIoSurfaceHandle;
#endif  // BUILDFLAG(IS_APPLE)
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OZONE)
    case gfx::NATIVE_PIXMAP:
      return Tag::kNativePixmapHandle;
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OZONE)
  }
  NOTREACHED();
}

bool UnionTraits<gfx::mojom::GpuMemoryBufferPlatformHandleDataView,
                 gfx::GpuMemoryBufferHandle>::
    IsNull(const gfx::GpuMemoryBufferHandle& handle) {
  return handle.type == gfx::EMPTY_BUFFER;
}

void UnionTraits<
    gfx::mojom::GpuMemoryBufferPlatformHandleDataView,
    gfx::GpuMemoryBufferHandle>::SetToNull(gfx::GpuMemoryBufferHandle* handle) {
  handle->type = gfx::EMPTY_BUFFER;
}

#if BUILDFLAG(IS_APPLE)
IOSurfaceHandle UnionTraits<gfx::mojom::GpuMemoryBufferPlatformHandleDataView,
                            gfx::GpuMemoryBufferHandle>::
    io_surface_handle(gfx::GpuMemoryBufferHandle& gmb_handle) {
  IOSurfaceHandle io_surface_handle;
  gfx::ScopedRefCountedIOSurfaceMachPort io_surface_mach_port;
  io_surface_handle.mach_send_right.reset(
      IOSurfaceCreateMachPort(gmb_handle.io_surface().get()));
  return io_surface_handle;
}
#endif  // BUILDFLAG(IS_APPLE)

bool UnionTraits<gfx::mojom::GpuMemoryBufferPlatformHandleDataView,
                 gfx::GpuMemoryBufferHandle>::
    Read(gfx::mojom::GpuMemoryBufferPlatformHandleDataView data,
         gfx::GpuMemoryBufferHandle* gmb_handle) {
  switch (data.tag()) {
    case Tag::kSharedMemoryHandle:
      gmb_handle->type = gfx::SHARED_MEMORY_BUFFER;
      return data.ReadSharedMemoryHandle(&gmb_handle->region_);
#if BUILDFLAG(IS_APPLE)
    case Tag::kIoSurfaceHandle:
      gmb_handle->type = gfx::IO_SURFACE_BUFFER;
      IOSurfaceHandle io_surface_handle;
      if (!data.ReadIoSurfaceHandle(&io_surface_handle)) {
        return false;
      }
      if (io_surface_handle.mach_send_right.is_valid()) {
        // This is expected to fail in sandboxed renderer processes on iOS.
        gmb_handle->io_surface_.reset(IOSurfaceLookupFromMachPort(
            io_surface_handle.mach_send_right.get()));
      } else {
        gmb_handle->io_surface_.reset();
      }
      return true;
#endif  // BUILDFLAG(IS_APPLE)
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OZONE)
    case Tag::kNativePixmapHandle:
      gmb_handle->type = gfx::NATIVE_PIXMAP;
      return data.ReadNativePixmapHandle(&gmb_handle->native_pixmap_handle_);
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OZONE)
  }
  return false;
}

}  // namespace mojo
