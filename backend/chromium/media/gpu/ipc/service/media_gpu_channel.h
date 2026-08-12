// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_IPC_SERVICE_MEDIA_GPU_CHANNEL_H_
#define MEDIA_GPU_IPC_SERVICE_MEDIA_GPU_CHANNEL_H_

#include "base/memory/raw_ptr.h"
#include "base/unguessable_token.h"

namespace gpu {
class GpuChannel;
}

namespace media {

class MediaGpuChannel {
 public:
  explicit MediaGpuChannel(gpu::GpuChannel* channel);
  MediaGpuChannel(const MediaGpuChannel&) = delete;
  MediaGpuChannel& operator=(const MediaGpuChannel&) = delete;
  ~MediaGpuChannel();

 private:
  const raw_ptr<gpu::GpuChannel, DanglingUntriaged> channel_;
};

}  // namespace media

#endif  // MEDIA_GPU_IPC_SERVICE_MEDIA_GPU_CHANNEL_H_
