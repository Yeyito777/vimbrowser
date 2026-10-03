// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/vulkan/vulkan_memory.h"

#include <vulkan/vulkan.h>

#include <optional>

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "build/build_config.h"
#include "gpu/vulkan/vulkan_device_queue.h"
#include "gpu/vulkan/vulkan_function_pointers.h"

namespace gpu {
namespace {

std::optional<uint32_t> FindMemoryTypeIndex(
    VkPhysicalDevice physical_device,
    const VkMemoryRequirements* requirements,
    VkMemoryPropertyFlags flags) {
  VkPhysicalDeviceMemoryProperties properties;
  vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
  constexpr uint32_t kMaxIndex = 31;
  for (uint32_t i = 0; i <= kMaxIndex; i++) {
    if (((1u << i) & requirements->memoryTypeBits) == 0) {
      continue;
    }
    if ((UNSAFE_TODO(properties.memoryTypes[i]).propertyFlags & flags) !=
        flags) {
      continue;
    }
    return i;
  }
  return std::nullopt;
}

}  // namespace

VulkanMemory::VulkanMemory(base::PassKey<VulkanMemory> pass_key) {}

VulkanMemory::~VulkanMemory() {
  DCHECK(!device_queue_);
  DCHECK(device_memory_ == VK_NULL_HANDLE);
}

// static
std::unique_ptr<VulkanMemory> VulkanMemory::Create(
    VulkanDeviceQueue* device_queue,
    VkDeviceMemory device_memory,
    VkDeviceSize size,
    uint32_t type_index) {
  auto memory = std::make_unique<VulkanMemory>(base::PassKey<VulkanMemory>());
  memory->device_queue_ = device_queue;
  memory->device_memory_ = device_memory;
  memory->size_ = size;
  memory->type_index_ = type_index;
  return memory;
}

// static
std::unique_ptr<VulkanMemory> VulkanMemory::Create(
    VulkanDeviceQueue* device_queue,
    const VkMemoryRequirements* requirements,
    const void* extra_allocate_info) {
  auto memory = std::make_unique<VulkanMemory>(base::PassKey<VulkanMemory>());
  if (!memory->Initialize(device_queue, requirements, extra_allocate_info)) {
    return nullptr;
  }
  return memory;
}

void VulkanMemory::Destroy() {
  if (!device_queue_) {
    return;
  }
  VkDevice vk_device = device_queue_->GetVulkanDevice();
  if (device_memory_ != VK_NULL_HANDLE) {
    vkFreeMemory(vk_device, device_memory_, nullptr /* pAllocator */);
    device_memory_ = VK_NULL_HANDLE;
  }
  device_queue_ = nullptr;
}

bool VulkanMemory::Initialize(VulkanDeviceQueue* device_queue,
                              const VkMemoryRequirements* requirements,
                              const void* extra_allocate_info) {
  auto index =
      FindMemoryTypeIndex(device_queue->GetVulkanPhysicalDevice(), requirements,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (!index) {
    // Fallback to use any driver advertised memory type when the preferred
    // DEVICE_LOCAL_BIT is not available.
    index = FindMemoryTypeIndex(device_queue->GetVulkanPhysicalDevice(),
                                requirements, 0 /* flags */);
  }
  if (!index) {
    DLOG(ERROR) << "Cannot find validate memory type index.";
    return false;
  }

  VkMemoryAllocateInfo memory_allocate_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = extra_allocate_info,
      .allocationSize = requirements->size,
      .memoryTypeIndex = index.value(),
  };

  VkDevice vk_device = device_queue->GetVulkanDevice();
  VkResult result = vkAllocateMemory(vk_device, &memory_allocate_info,
                                     nullptr /* pAllocator */, &device_memory_);
  if (result != VK_SUCCESS) {
    DLOG(ERROR) << "vkAllocateMemory failed result:" << result;
    return false;
  }

  device_queue_ = device_queue;
  size_ = requirements->size;
  type_index_ = index.value();

  return true;
}

base::ScopedFD VulkanMemory::GetMemoryFd(
    VkExternalMemoryHandleTypeFlagBits handle_type) {
  VkMemoryGetFdInfoKHR get_fd_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
      .memory = device_memory_,
      .handleType = handle_type,

  };

  VkDevice device = device_queue_->GetVulkanDevice();
  int memory_fd = -1;
  vkGetMemoryFdKHR(device, &get_fd_info, &memory_fd);
  if (memory_fd < 0) {
    DLOG(ERROR) << "Unable to extract file descriptor out of external VkImage";
    return base::ScopedFD();
  }

  return base::ScopedFD(memory_fd);
}



}  // namespace gpu
