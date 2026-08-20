// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_VIDEO_CAPTURE_PUBLIC_CPP_MOCK_VIDEO_CAPTURE_SERVICE_H_
#define SERVICES_VIDEO_CAPTURE_PUBLIC_CPP_MOCK_VIDEO_CAPTURE_SERVICE_H_

#include "build/build_config.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "services/video_capture/public/mojom/video_capture_service.mojom.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace video_capture {

class MockVideoCaptureService
    : public video_capture::mojom::VideoCaptureService {
 public:
  MockVideoCaptureService();
  ~MockVideoCaptureService() override;

  void ConnectToVideoSourceProvider(
      mojo::PendingReceiver<video_capture::mojom::VideoSourceProvider> receiver)
      override;


  void BindControlsForTesting(
      mojo::PendingReceiver<mojom::TestingControls>) override {}

  MOCK_METHOD1(SetShutdownDelayInSeconds, void(float seconds));
  MOCK_METHOD1(DoConnectToVideoSourceProvider,
               void(mojo::PendingReceiver<
                    video_capture::mojom::VideoSourceProvider> receiver));

};

}  // namespace video_capture

#endif  // SERVICES_VIDEO_CAPTURE_PUBLIC_CPP_MOCK_VIDEO_CAPTURE_SERVICE_H_
