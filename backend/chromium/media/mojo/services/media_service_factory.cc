// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/mojo/services/media_service_factory.h"

#include <memory>

#include "base/notreached.h"
#include "build/build_config.h"
#include "media/mojo/buildflags.h"
#include "media/mojo/services/gpu_mojo_media_client.h"
#include "media/mojo/services/media_service.h"
#include "media/mojo/services/test_mojo_media_client.h"


namespace media {

std::unique_ptr<MediaService> CreateMediaService(
    mojo::PendingReceiver<mojom::MediaService> receiver) {
  NOTREACHED() << "No MediaService implementation available.";
}

std::unique_ptr<MediaService> CreateGpuMediaService(
    mojo::PendingReceiver<mojom::MediaService> receiver,
    std::unique_ptr<GpuMojoMediaClient> client) {
  return std::make_unique<MediaService>(std::move(client), std::move(receiver));
}

std::unique_ptr<MediaService> CreateMediaServiceForTesting(
    mojo::PendingReceiver<mojom::MediaService> receiver) {
  return std::make_unique<MediaService>(std::make_unique<TestMojoMediaClient>(),
                                        std::move(receiver));
}

}  // namespace media
