// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PARTITION_ALLOC_PARTITION_ALLOC_BASE_PROCESS_PROCESS_HANDLE_H_
#define PARTITION_ALLOC_PARTITION_ALLOC_BASE_PROCESS_PROCESS_HANDLE_H_

#include <sys/types.h>

#include <cstdint>

#include "partition_alloc/build_config.h"
#include "partition_alloc/partition_alloc_base/component_export.h"



namespace partition_alloc::internal::base {

// ProcessHandle is a platform specific type which represents the underlying OS
// handle to a process.
// ProcessId is a number which identifies the process in the OS.
// On POSIX, our ProcessHandle will just be the PID.
typedef pid_t ProcessId;
const ProcessId kNullProcessId = 0;

// Returns the id of the current process.
// Note that on some platforms, this is not guaranteed to be unique across
// processes (use GetUniqueIdForProcess if uniqueness is required).
PA_COMPONENT_EXPORT(PARTITION_ALLOC_BASE) ProcessId GetCurrentProcId();

}  // namespace partition_alloc::internal::base

#endif  // PARTITION_ALLOC_PARTITION_ALLOC_BASE_PROCESS_PROCESS_HANDLE_H_
