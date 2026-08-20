// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_CORE_PLATFORM_HANDLE_IN_TRANSIT_H_
#define MOJO_CORE_PLATFORM_HANDLE_IN_TRANSIT_H_

#include "base/process/process.h"
#include "build/build_config.h"
#include "mojo/public/cpp/platform/platform_handle.h"


namespace mojo {
namespace core {

// Owns a PlatformHandle which may actually belong to another process. On
// Windows and (sometimes) Mac, handles in a message object may take on values
// which only have meaning in the context of a remote process.
//
// This class provides a safe way of scoping the lifetime of such handles so
// that they don't leak when transmission can't be completed.
class PlatformHandleInTransit {
 public:
  PlatformHandleInTransit();
  explicit PlatformHandleInTransit(PlatformHandle handle);
  PlatformHandleInTransit(PlatformHandleInTransit&&);

  PlatformHandleInTransit(const PlatformHandleInTransit&) = delete;
  PlatformHandleInTransit& operator=(const PlatformHandleInTransit&) = delete;

  ~PlatformHandleInTransit();

  PlatformHandleInTransit& operator=(PlatformHandleInTransit&&);

  // Accessor for the owned handle. Must be owned by the calling process.
  const PlatformHandle& handle() const {
    DCHECK(!owning_process_.IsValid());
    return handle_;
  }

  // Returns the process which owns this handle. If this is invalid, the handle
  // is owned by the current process.
  const base::Process& owning_process() const { return owning_process_; }

  // Takes ownership of the held handle as-is. The handle must belong to the
  // current process.
  PlatformHandle TakeHandle();

  // Discards the handle owned by this object. The implication is that its
  // value has been successfully communicated to the owning process and the
  // calling process is no longer responsible for managing the handle's
  // lifetime.
  void CompleteTransit();

  // Designates the relative trust level of the destination process compared to
  // the source process, in the context of a handle transfer operation. This
  // may be expanded to more granular degrees of trust in the future.
  enum TransferTargetTrustLevel {
    // No special constraints on what can be transferred or how.
    kTrustedTarget,

    // On some platforms, transfers with this destination type may be restricted
    // to block certain types of handles.
    kUntrustedTarget,
  };

  // Transfers ownership of this (local) handle to |target_process|.
  bool TransferToProcess(base::Process target_process,
                         TransferTargetTrustLevel trust = kTrustedTarget);


 private:

  PlatformHandle handle_;
  base::Process owning_process_;
};

}  // namespace core
}  // namespace mojo

#endif  // MOJO_CORE_PLATFORM_HANDLE_IN_TRANSIT_H_
