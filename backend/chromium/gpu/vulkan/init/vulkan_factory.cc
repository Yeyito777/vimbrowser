// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/vulkan/init/vulkan_factory.h"
#include "build/build_config.h"

#include <memory>
#include <ostream>


#if BUILDFLAG(IS_OZONE)
#include "ui/ozone/public/ozone_platform.h"
#include "ui/ozone/public/surface_factory_ozone.h"
#endif

#if BUILDFLAG(IS_APPLE)
#include "gpu/vulkan/mac/vulkan_implementation_mac.h"
#endif

namespace gpu {

std::unique_ptr<VulkanImplementation> CreateVulkanImplementation(
    bool use_swiftshader,
    bool allow_protected_memory) {
#if BUILDFLAG(IS_OZONE)
  return ui::OzonePlatform::GetInstance()
      ->GetSurfaceFactoryOzone()
      ->CreateVulkanImplementation(use_swiftshader, allow_protected_memory);
#else

  // TODO(samans): Support Swiftshader on more platforms.
  // https://crbug.com/963988
  DCHECK(!use_swiftshader)
      << "Vulkan Swiftshader is not supported on this platform.";

  // Protected memory is supported only on Fuchsia, which uses Ozone, i.e.
  // VulkanImplementation is initialized above.
  DCHECK(!allow_protected_memory)
      << "Protected memory is not supported on this platform.";

#if BUILDFLAG(IS_APPLE)
  return std::make_unique<VulkanImplementationMac>(use_swiftshader);
#else
  NOTREACHED();
#endif
#endif
}

}  // namespace gpu
