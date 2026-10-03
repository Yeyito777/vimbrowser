// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/video_capture_buffer_pool_util.h"

#include "build/build_config.h"
#include "media/base/media_switches.h"
#include "media/capture/capture_switches.h"

namespace media {

int DeviceVideoCaptureMaxBufferPoolSize() {
  // The maximum number of video frame buffers in-flight at any one time.
  // If all buffers are still in use by consumers when new frames are produced
  // those frames get dropped.
  static int max_buffer_count = kVideoCaptureDefaultMaxBufferPoolSize;

#if BUILDFLAG(IS_APPLE)
  // On macOS, we allow a few more buffers as it's routinely observed that it
  // runs out of three when just displaying 60 FPS media in a video element.
  // Also, this must be greater than the frame delay of VideoToolbox encoder
  // for AVC High Profile.
  max_buffer_count = 15;
#endif

  return max_buffer_count;
}

}  // namespace media
