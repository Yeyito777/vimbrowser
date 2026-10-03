// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/vulkan/semaphore_handle.h"

#include "base/logging.h"
#include "build/build_config.h"

#include <unistd.h>
#include "base/posix/eintr_wrapper.h"



namespace gpu {

SemaphoreHandle::SemaphoreHandle() = default;
SemaphoreHandle::SemaphoreHandle(VkExternalSemaphoreHandleTypeFlagBits type,
                                 PlatformHandle handle)
    : type_(type), handle_(std::move(handle)) {}
SemaphoreHandle::SemaphoreHandle(SemaphoreHandle&&) = default;

SemaphoreHandle::~SemaphoreHandle() = default;

SemaphoreHandle& SemaphoreHandle::operator=(SemaphoreHandle&&) = default;

SemaphoreHandle::SemaphoreHandle(gfx::GpuFenceHandle fence_handle) {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
  Init(VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT_KHR,
       fence_handle.Release());
#else
  Init(VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
       fence_handle.Release());
#endif  // BUILDFLAG(IS_FUCHSIA)
}

void SemaphoreHandle::Init(VkExternalSemaphoreHandleTypeFlagBits type,
                           PlatformHandle handle) {
  type_ = type;
  handle_ = std::move(handle);
}

gfx::GpuFenceHandle SemaphoreHandle::ToGpuFenceHandle() && {
  gfx::GpuFenceHandle fence_handle;
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
  if (type_ == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT_KHR) {
    fence_handle.Adopt(TakeHandle());
  } else {
    DLOG(ERROR) << "Unable to convert SemaphoreHandle to GpuFenceHandle";
  }
#else
  fence_handle.Adopt(TakeHandle());
#endif  // BUILDFLAG(IS_FUCHSIA)
  return fence_handle;
}

SemaphoreHandle SemaphoreHandle::Duplicate() const {
  if (!is_valid())
    return SemaphoreHandle();

  return SemaphoreHandle(type_,
                         base::ScopedFD(HANDLE_EINTR(dup(handle_.get()))));
}

}  // namespace gpu
