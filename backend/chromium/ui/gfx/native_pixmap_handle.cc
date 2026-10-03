// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/native_pixmap_handle.h"

#include <utility>

#include "base/logging.h"
#include "build/build_config.h"
#include "components/viz/common/resources/shared_image_format_utils.h"
#include "ui/gfx/geometry/size.h"

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
#include <drm_fourcc.h>
#include <unistd.h>

#include "base/posix/eintr_wrapper.h"
#endif


namespace gfx {

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
static_assert(NativePixmapHandle::kNoModifier == DRM_FORMAT_MOD_INVALID,
              "gfx::NativePixmapHandle::kNoModifier should be an alias for"
              "DRM_FORMAT_MOD_INVALID");
#endif

NativePixmapPlane::NativePixmapPlane() : stride(0), offset(0), size(0) {}

NativePixmapPlane::NativePixmapPlane(int stride,
                                     int offset,
                                     uint64_t size
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
                                     ,
                                     base::ScopedFD fd
#endif
                                     )
    : stride(stride),
      offset(offset),
      size(size)
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
      ,
      fd(std::move(fd))
#endif
{
}

NativePixmapPlane::NativePixmapPlane(NativePixmapPlane&& other) = default;

NativePixmapPlane::~NativePixmapPlane() = default;

NativePixmapPlane& NativePixmapPlane::operator=(NativePixmapPlane&& other) =
    default;

NativePixmapHandle::NativePixmapHandle() = default;
NativePixmapHandle::NativePixmapHandle(NativePixmapHandle&& other) = default;

NativePixmapHandle::~NativePixmapHandle() = default;

NativePixmapHandle& NativePixmapHandle::operator=(NativePixmapHandle&& other) =
    default;

NativePixmapHandle CloneHandleForIPC(const NativePixmapHandle& handle) {
  NativePixmapHandle clone;
  for (auto& plane : handle.planes) {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    DCHECK(plane.fd.is_valid());
    // Combining the HANDLE_EINTR and ScopedFD's constructor causes the compiler
    // to emit some very strange assembly that tends to cause FD ownership
    // violations. see crbug.com/c/1287325.
    int checked_dup = HANDLE_EINTR(dup(plane.fd.get()));
    base::ScopedFD fd_dup(checked_dup);
    if (!fd_dup.is_valid()) {
      PLOG(ERROR) << "dup";
      return NativePixmapHandle();
    }
    NativePixmapPlane cloned_plane;
    cloned_plane.stride = plane.stride;
    cloned_plane.offset = plane.offset;
    cloned_plane.size = plane.size;
    cloned_plane.fd = std::move(fd_dup);
    clone.planes.push_back(std::move(cloned_plane));
#else
#error Unsupported OS
#endif
  }

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  clone.modifier = handle.modifier;
  clone.supports_zero_copy_webgpu_import =
      handle.supports_zero_copy_webgpu_import;
#endif


  return clone;
}

bool CanFitImageForSizeAndFormat(const gfx::NativePixmapHandle& handle,
                                 const gfx::Size& size,
                                 viz::SharedImageFormat format,
                                 bool assume_single_memory_object) {
  size_t expected_planes = format.NumberOfPlanes();
  if (expected_planes == 0 || handle.planes.size() != expected_planes)
    return false;

  size_t total_size = 0u;
  if (assume_single_memory_object) {
    if (!base::IsValueInRangeForNumericType<size_t>(
            handle.planes.back().offset) ||
        !base::IsValueInRangeForNumericType<size_t>(
            handle.planes.back().size)) {
      return false;
    }
    const base::CheckedNumeric<size_t> total_size_checked =
        base::CheckAdd(base::checked_cast<size_t>(handle.planes.back().offset),
                       base::checked_cast<size_t>(handle.planes.back().size));
    if (!total_size_checked.IsValid())
      return false;
    total_size = total_size_checked.ValueOrDie();
  }

  for (size_t i = 0; i < handle.planes.size(); ++i) {
    const size_t plane_stride =
        base::strict_cast<size_t>(handle.planes[i].stride);

    auto min_stride = viz::SharedMemoryRowSizeForSharedImageFormat(
        format, i, base::checked_cast<size_t>(size.width()));
    if (!min_stride || plane_stride < min_stride.value()) {
      return false;
    }

    const base::CheckedNumeric<size_t> plane_height =
        format.GetPlaneSize(i, size).height();
    const base::CheckedNumeric<size_t> min_size = plane_height * plane_stride;
    if (!min_size.IsValid<uint64_t>() ||
        handle.planes[i].size < min_size.ValueOrDie<uint64_t>()) {
      return false;
    }

    // The stride must be a valid integer in order to be consistent with the
    // GpuMemoryBuffer::stride()/gfx::ClientNativePixmap::GetStride() APIs.
    // Also, refer to http://crbug.com/1093644#c1 for some comments on this
    // check and others in this method.
    if (!base::IsValueInRangeForNumericType<int>(plane_stride))
      return false;

    if (assume_single_memory_object) {
      if (!base::IsValueInRangeForNumericType<size_t>(
              handle.planes[i].offset) ||
          !base::IsValueInRangeForNumericType<size_t>(handle.planes[i].size)) {
        return false;
      }
      base::CheckedNumeric<size_t> end_pos =
          base::CheckAdd(base::checked_cast<size_t>(handle.planes[i].offset),
                         base::checked_cast<size_t>(handle.planes[i].size));
      if (!end_pos.IsValid() || end_pos.ValueOrDie() > total_size)
        return false;
    }
  }

  return true;
}

}  // namespace gfx
