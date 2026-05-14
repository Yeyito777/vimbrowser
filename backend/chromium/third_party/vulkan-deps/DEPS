# This file is used to manage Vulkan dependencies for several repos. It is
# used by gclient to determine what version of each dependency to check out, and
# where.

# Avoids the need for a custom root variable.
use_relative_paths = True
git_dependencies = 'SYNC'

vars = {
  'chromium_git': 'https://chromium.googlesource.com',

  # Current revision of glslang, the Khronos SPIRV compiler.
  'glslang_revision': 'b11b03839c940685b0201026bd2a4ffef1d5a4b8',

  # Current revision of Lunarg VulkanTools
  'lunarg_vulkantools_revision': '5293aca8308eb936048f5149ab7c4a883ae5e63f',

  # Current revision of spirv-cross, the Khronos SPIRV cross compiler.
  'spirv_cross_revision': 'b8fcf307f1f347089e3c46eb4451d27f32ebc8d3',

  # Current revision fo the SPIRV-Headers Vulkan support library.
  'spirv_headers_revision': 'f88a2d766840fc825af1fc065977953ba1fa4a91',

  # Current revision of SPIRV-Tools for Vulkan.
  'spirv_tools_revision': '7d8d9e58c384949f1615c069d4c9346bf51b9738',

  # Current revision of Khronos Vulkan-Headers.
  'vulkan_headers_revision': '74d8a6cb930c68ef617b202c3ff3c59d919e086b',

  # Current revision of Khronos Vulkan-Loader.
  'vulkan_loader_revision': '363f465abadab0a8dcfc5c85d2c691e9b0b788d6',

  # Current revision of Khronos Vulkan-Tools.
  'vulkan_tools_revision': '59f963ce1b1d16cc92137a241a0fe98d637d21f4',

  # Current revision of Khronos Vulkan-Utility-Libraries.
  'vulkan_utility_libraries_revision': '20fb10eb1ec08ccd5cacec32b7df1b0e99e48a0c',

  # Current revision of Khronos Vulkan-ValidationLayers.
  'vulkan_validation_revision': '20948525099c0ea030ec5b149c809e48010be4ed',
}

deps = {
  'glslang/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/glslang@{glslang_revision}',
  },

  'lunarg-vulkantools/src': {
    'url': '{chromium_git}/external/github.com/LunarG/VulkanTools@{lunarg_vulkantools_revision}',
  },

  'spirv-cross/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/SPIRV-Cross@{spirv_cross_revision}',
  },

  'spirv-headers/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/SPIRV-Headers@{spirv_headers_revision}',
  },

  'spirv-tools/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/SPIRV-Tools@{spirv_tools_revision}',
  },

  'vulkan-headers/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/Vulkan-Headers@{vulkan_headers_revision}',
  },

  'vulkan-loader/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/Vulkan-Loader@{vulkan_loader_revision}',
  },

  'vulkan-tools/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/Vulkan-Tools@{vulkan_tools_revision}',
  },

  'vulkan-utility-libraries/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/Vulkan-Utility-Libraries@{vulkan_utility_libraries_revision}',
  },

  'vulkan-validation-layers/src': {
    'url': '{chromium_git}/external/github.com/KhronosGroup/Vulkan-ValidationLayers@{vulkan_validation_revision}',
  },
}
