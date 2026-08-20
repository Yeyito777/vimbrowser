// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/video_capture_buffer_tracker_factory_impl.h"

#include <memory>

#include "build/build_config.h"
#include "media/capture/video/shared_image_buffer_tracker.h"
#include "media/capture/video/shared_memory_buffer_tracker.h"

#if BUILDFLAG(IS_APPLE)
#include "media/capture/video/apple/gpu_memory_buffer_tracker_apple.h"
#elif BUILDFLAG(IS_LINUX)
#include "media/capture/video/linux/v4l2_gpu_memory_buffer_tracker.h"
#endif

namespace media {

VideoCaptureBufferTrackerFactoryImpl::VideoCaptureBufferTrackerFactoryImpl() {}


VideoCaptureBufferTrackerFactoryImpl::~VideoCaptureBufferTrackerFactoryImpl() =
    default;

std::unique_ptr<VideoCaptureBufferTracker>
VideoCaptureBufferTrackerFactoryImpl::CreateTracker(
    VideoCaptureBufferType buffer_type) {
  switch (buffer_type) {
    case VideoCaptureBufferType::kGpuMemoryBuffer:
#if BUILDFLAG(IS_APPLE)
      return std::make_unique<GpuMemoryBufferTrackerApple>();
#elif BUILDFLAG(IS_LINUX)
      return std::make_unique<V4L2GpuMemoryBufferTracker>();
#else
      return nullptr;
#endif
    case VideoCaptureBufferType::kSharedImage:
      return nullptr;
    default:
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
      // Since Windows and macOS capturer outputs NV12 only for GMBs and I420
      // for software frames, the pixel format is used to choose between shmem
      // and gmb trackers. Therefore I420 shmem trackers must not be reusable
      // for NV12 format.
      return std::make_unique<SharedMemoryBufferTracker>(
          /*reusable_only_for_same_format=*/true);
#else
      return std::make_unique<SharedMemoryBufferTracker>();
#endif
  }
}

std::unique_ptr<VideoCaptureBufferTracker>
VideoCaptureBufferTrackerFactoryImpl::CreateTrackerForExternalBuffer(
    CapturedExternalVideoBuffer buffer) {
  if (buffer.client_shared_image) {
    return std::make_unique<SharedImageBufferTracker>(
        std::move(buffer.client_shared_image));
  }
  gfx::GpuMemoryBufferHandle handle = std::move(buffer.handle);
#if BUILDFLAG(IS_APPLE)
  return std::make_unique<GpuMemoryBufferTrackerApple>(handle.io_surface());
#else
  return nullptr;
#endif
}

}  // namespace media
