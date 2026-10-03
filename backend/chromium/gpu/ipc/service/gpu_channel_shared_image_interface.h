// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_IPC_SERVICE_GPU_CHANNEL_SHARED_IMAGE_INTERFACE_H_
#define GPU_IPC_SERVICE_GPU_CHANNEL_SHARED_IMAGE_INTERFACE_H_

#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "gpu/command_buffer/client/client_shared_image.h"
#include "gpu/command_buffer/client/shared_image_interface.h"
#include "gpu/command_buffer/common/command_buffer_id.h"
#include "gpu/command_buffer/common/shared_image_capabilities.h"
#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/command_buffer/service/sequence_id.h"
#include "gpu/command_buffer/service/shared_image_interface_in_process_base.h"
#include "gpu/gpu_gles2_export.h"
#include "gpu/ipc/common/gpu_memory_buffer_handle_info.h"
#include "gpu/ipc/service/shared_image_stub.h"
#include "ui/gfx/gpu_memory_buffer_handle.h"


namespace gpu {
class Scheduler;

class GPU_IPC_SERVICE_EXPORT GpuChannelSharedImageInterface
    : public SharedImageInterfaceInProcessBase {
 public:
  explicit GpuChannelSharedImageInterface(
      base::WeakPtr<SharedImageStub> shared_image_stub);

  GpuChannelSharedImageInterface(const GpuChannelSharedImageInterface&) =
      delete;
  GpuChannelSharedImageInterface& operator=(
      const GpuChannelSharedImageInterface&) = delete;


  SequenceId sequence() { return sequence_; }

 protected:
  ~GpuChannelSharedImageInterface() override;

  // SharedImageInterfaceInProcessBase:
  void ScheduleGpuTask(base::OnceClosure task,
                       std::vector<SyncToken> sync_token_fences,
                       const SyncToken& release) override;
  SharedImageFactory* GetSharedImageFactoryOnGpuThread() override;
  bool MakeContextCurrentOnGpuThread(bool needs_gl) override;
  void MarkContextLostOnGpuThread() override;

 private:
  base::WeakPtr<SharedImageStub> shared_image_stub_;

  raw_ptr<Scheduler> scheduler_;
  const SequenceId sequence_;
};

}  // namespace gpu

#endif  // GPU_IPC_SERVICE_GPU_CHANNEL_SHARED_IMAGE_INTERFACE_H_
