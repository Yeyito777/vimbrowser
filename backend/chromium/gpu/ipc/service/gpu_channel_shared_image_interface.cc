// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/ipc/service/gpu_channel_shared_image_interface.h"

#include "base/synchronization/waitable_event.h"
#include "build/build_config.h"
#include "components/viz/common/resources/shared_image_format_utils.h"
#include "gpu/command_buffer/client/client_shared_image.h"
#include "gpu/command_buffer/common/shared_image_capabilities.h"
#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/command_buffer/common/sync_token.h"
#include "gpu/command_buffer/service/scheduler.h"
#include "gpu/command_buffer/service/shared_image/shared_image_factory.h"
#include "gpu/command_buffer/service/shared_image_interface_in_process_base.h"
#include "gpu/ipc/service/gpu_channel.h"
#include "gpu/ipc/service/gpu_channel_manager.h"

namespace gpu {

namespace {

// Generates unique IDs for GpuChannelSharedImageInterface.
base::AtomicSequenceNumber g_next_id;

}  // namespace

GpuChannelSharedImageInterface::GpuChannelSharedImageInterface(
    base::WeakPtr<SharedImageStub> shared_image_stub)
    : SharedImageInterfaceInProcessBase(
          CommandBufferNamespace::GPU_CHANNEL_SHARED_IMAGE_INTERFACE,
          CommandBufferIdFromChannelAndRoute(
              shared_image_stub->channel()->client_id(),
              g_next_id.GetNext() + 1),
          /*verify_creation_sync_token=*/true,
          shared_image_stub->factory()->MakeCapabilities()),
      shared_image_stub_(shared_image_stub),
      scheduler_(shared_image_stub->channel()->scheduler()),
      sequence_(scheduler_->CreateSequence(
          SchedulingPriority::kLow,
          shared_image_stub->channel()->task_runner(),
          CommandBufferNamespace::GPU_CHANNEL_SHARED_IMAGE_INTERFACE,
          command_buffer_id())) {}

GpuChannelSharedImageInterface::~GpuChannelSharedImageInterface() {
  scheduler_->DestroySequence(sequence_);
}


SharedImageFactory*
GpuChannelSharedImageInterface::GetSharedImageFactoryOnGpuThread() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(gpu_sequence_checker_);
  if (!shared_image_stub_) {
    return nullptr;
  }
  return shared_image_stub_->factory();
}

bool GpuChannelSharedImageInterface::MakeContextCurrentOnGpuThread(
    bool needs_gl) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(gpu_sequence_checker_);
  if (!shared_image_stub_) {
    return false;
  }

  return shared_image_stub_->MakeContextCurrent(needs_gl);
}

void GpuChannelSharedImageInterface::MarkContextLostOnGpuThread() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(gpu_sequence_checker_);
  shared_image_stub_->shared_context_state()->MarkContextLost();
}

void GpuChannelSharedImageInterface::ScheduleGpuTask(
    base::OnceClosure task,
    std::vector<SyncToken> sync_token_fences,
    const SyncToken& release) {
  scheduler_->ScheduleTask(gpu::Scheduler::Task(
      sequence_, std::move(task), std::move(sync_token_fences), release));
}

}  // namespace gpu
