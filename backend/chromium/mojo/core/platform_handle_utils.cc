// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/core/platform_handle_utils.h"

#include "build/build_config.h"

#include "base/files/scoped_file.h"

#if BUILDFLAG(IS_APPLE)
#include "base/apple/scoped_mach_port.h"
#endif

namespace mojo {
namespace core {

void ExtractPlatformHandlesFromSharedMemoryRegionHandle(
    base::subtle::ScopedPlatformSharedMemoryHandle handle,
    PlatformHandle* extracted_handle,
    PlatformHandle* extracted_readonly_handle) {
#if BUILDFLAG(IS_APPLE)
  // This is a Mach port. Same code as above and below, but separated for
  // clarity.
  *extracted_handle = PlatformHandle(std::move(handle));
#else
  *extracted_handle = PlatformHandle(std::move(handle.fd));
  *extracted_readonly_handle = PlatformHandle(std::move(handle.readonly_fd));
#endif
}

base::subtle::ScopedPlatformSharedMemoryHandle
CreateSharedMemoryRegionHandleFromPlatformHandles(
    PlatformHandle handle,
    PlatformHandle readonly_handle) {
#if BUILDFLAG(IS_APPLE)
  DCHECK(!readonly_handle.is_valid());
  return handle.TakeMachSendRight();
#else
  return base::subtle::ScopedFDPair(handle.TakeFD(), readonly_handle.TakeFD());
#endif
}

MojoResult UnwrapAndClonePlatformProcessHandle(
    const MojoPlatformProcessHandle* process_handle,
    base::Process& process) {
  if (process_handle->struct_size < sizeof(*process_handle)) {
    return MOJO_RESULT_INVALID_ARGUMENT;
  }

  base::ProcessHandle in_handle =
      static_cast<base::ProcessHandle>(process_handle->value);

  if (in_handle == base::kNullProcessHandle) {
    process = base::Process();
    return MOJO_RESULT_OK;
  }

  process = base::Process(in_handle);
  return MOJO_RESULT_OK;
}

}  // namespace core
}  // namespace mojo
