// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_VR_OPENXR_OPENXR_SWAPCHAIN_INFO_H_
#define DEVICE_VR_OPENXR_OPENXR_SWAPCHAIN_INFO_H_

#include "gpu/command_buffer/client/client_shared_image.h"
#include "ui/gfx/geometry/size.h"



namespace device {

// TODO(crbug.com/40909689): Refactor this class.
struct OpenXrSwapchainInfo {
 public:
  OpenXrSwapchainInfo();
  virtual ~OpenXrSwapchainInfo();
  OpenXrSwapchainInfo(OpenXrSwapchainInfo&&);
  OpenXrSwapchainInfo& operator=(OpenXrSwapchainInfo&&);

  void Clear();

  scoped_refptr<gpu::ClientSharedImage> shared_image;
  gpu::SyncToken sync_token;

};

}  // namespace device

#endif  // DEVICE_VR_OPENXR_OPENXR_SWAPCHAIN_INFO_H_
