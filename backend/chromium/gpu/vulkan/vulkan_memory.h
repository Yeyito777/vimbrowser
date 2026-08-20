// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_VULKAN_VULKAN_MEMORY_H_
#define GPU_VULKAN_VULKAN_MEMORY_H_

#include <vulkan/vulkan_core.h>

#include "base/component_export.h"
#include "base/files/scoped_file.h"
#include "base/memory/raw_ptr.h"
#include "base/types/pass_key.h"
#include "build/build_config.h"



namespace gpu {

class VulkanDeviceQueue;

class COMPONENT_EXPORT(VULKAN) VulkanMemory {
 public:
  explicit VulkanMemory(base::PassKey<VulkanMemory> pass_key);
  ~VulkanMemory();

  VulkanMemory(VulkanMemory&) = delete;
  VulkanMemory& operator=(VulkanMemory&) = delete;

  static std::unique_ptr<VulkanMemory> Create(VulkanDeviceQueue* device_queue,
                                              VkDeviceMemory device_memory,
                                              VkDeviceSize size,
                                              uint32_t type_index);

  static std::unique_ptr<VulkanMemory> Create(
      VulkanDeviceQueue* device_queue,
      const VkMemoryRequirements* requirements,
      const void* extra_allocate_info);

  void Destroy();

  base::ScopedFD GetMemoryFd(VkExternalMemoryHandleTypeFlagBits handle_type);



  VulkanDeviceQueue* device_queue() const { return device_queue_; }
  VkDeviceSize size() const { return size_; }
  uint32_t type_index() const { return type_index_; }
  VkDeviceMemory device_memory() const { return device_memory_; }

 private:
  bool Initialize(VulkanDeviceQueue* device_queue,
                  const VkMemoryRequirements* requirements,
                  const void* extra_allocate_info);

  raw_ptr<VulkanDeviceQueue> device_queue_ = nullptr;
  VkDeviceMemory device_memory_ = VK_NULL_HANDLE;
  VkDeviceSize size_ = 0;
  uint32_t type_index_ = 0;
};

}  // namespace gpu

#endif  // GPU_VULKAN_VULKAN_MEMORY_H_
