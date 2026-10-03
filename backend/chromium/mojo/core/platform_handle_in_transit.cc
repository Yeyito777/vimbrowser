// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/core/platform_handle_in_transit.h"

#include <utility>

#include "base/debug/alias.h"
#include "base/logging.h"
#include "base/process/process_handle.h"
#include "build/build_config.h"


namespace mojo {
namespace core {

namespace {


}  // namespace

PlatformHandleInTransit::PlatformHandleInTransit() = default;

PlatformHandleInTransit::PlatformHandleInTransit(PlatformHandle handle)
    : handle_(std::move(handle)) {}

PlatformHandleInTransit::PlatformHandleInTransit(
    PlatformHandleInTransit&& other) {
  *this = std::move(other);
}

PlatformHandleInTransit::~PlatformHandleInTransit() {
}

PlatformHandleInTransit& PlatformHandleInTransit::operator=(
    PlatformHandleInTransit&& other) {
  handle_ = std::move(other.handle_);
  owning_process_ = std::move(other.owning_process_);
  return *this;
}

PlatformHandle PlatformHandleInTransit::TakeHandle() {
  DCHECK(!owning_process_.IsValid());
  return std::move(handle_);
}

void PlatformHandleInTransit::CompleteTransit() {
  handle_.release();
  owning_process_ = base::Process();
}

bool PlatformHandleInTransit::TransferToProcess(
    base::Process target_process,
    TransferTargetTrustLevel trust) {
  DCHECK(target_process.IsValid());
  DCHECK(!owning_process_.IsValid());
  DCHECK(handle_.is_valid());
  owning_process_ = std::move(target_process);
  return true;
}


}  // namespace core
}  // namespace mojo
