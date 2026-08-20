// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_VIDEO_CAPTURE_DEVICE_MEDIA_TO_MOJO_ADAPTER_H_
#define SERVICES_VIDEO_CAPTURE_DEVICE_MEDIA_TO_MOJO_ADAPTER_H_

#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread_checker.h"
#include "build/build_config.h"
#include "media/capture/video/video_capture_device.h"
#include "media/capture/video/video_capture_device_client.h"
#include "media/capture/video/video_capture_device_factory.h"
#include "media/capture/video_capture_types.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/video_capture/device.h"
#include "services/video_capture/public/mojom/device.mojom.h"
#include "services/video_capture/public/mojom/video_frame_handler.mojom.h"


namespace video_capture {

class ReceiverMojoToMediaAdapter;

// Implementation of mojom::Device backed by a given instance of
// media::VideoCaptureDevice.
class DeviceMediaToMojoAdapter : public Device {
 public:
  DeviceMediaToMojoAdapter(
      std::unique_ptr<media::VideoCaptureDevice> device);
  ~DeviceMediaToMojoAdapter() override;

  // Device implementation.
  void Start(const media::VideoCaptureParams& requested_settings,
             mojo::PendingRemote<mojom::VideoFrameHandler>
                 handler_pending_remote) override;
  void StartInProcess(
      const media::VideoCaptureParams& requested_settings,
      const base::WeakPtr<media::VideoFrameReceiver>& frame_handler) override;
  void StopInProcess() override;
  void MaybeSuspend() override;
  void Resume() override;
  void GetPhotoState(GetPhotoStateCallback callback) override;
  void SetPhotoOptions(media::mojom::PhotoSettingsPtr settings,
                       SetPhotoOptionsCallback callback) override;
  void TakePhoto(TakePhotoCallback callback) override;
  void ProcessFeedback(const media::VideoCaptureFeedback& feedback) override;
  void RequestRefreshFrame() override;

  void Stop();
  void OnClientConnectionErrorOrClose();

  // Returns the fixed maximum number of buffers passed to the constructor
  // of VideoCaptureBufferPoolImpl.
  static int max_buffer_pool_buffer_count();

 private:
  void StartInternal(
      const media::VideoCaptureParams& requested_settings,
      std::optional<mojo::PendingRemote<mojom::VideoFrameHandler>>
          handler_pending_remote,
      const base::WeakPtr<media::VideoFrameReceiver>& frame_handler,
      bool start_in_process);

  const std::unique_ptr<media::VideoCaptureDevice> device_;
  std::unique_ptr<ReceiverMojoToMediaAdapter> receiver_;
  bool device_started_ = false;
  base::ThreadChecker thread_checker_;
  base::WeakPtrFactory<DeviceMediaToMojoAdapter> weak_factory_{this};
};

}  // namespace video_capture

#endif  // SERVICES_VIDEO_CAPTURE_DEVICE_MEDIA_TO_MOJO_ADAPTER_H_
