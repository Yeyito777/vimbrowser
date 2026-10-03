// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/debug/profiler.h"

#include "base/allocator/buildflags.h"
#include "base/check.h"
#include "base/process/process_handle.h"
#include "build/build_config.h"


namespace base::debug {

void StartProfiling(const std::string& name) {}

void StopProfiling() {}

void FlushProfiling() {}

bool BeingProfiled() {
  return false;
}

void RestartProfilingAfterFork() {}

bool IsProfilingSupported() {
  return false;
}


ReturnAddressLocationResolver GetProfilerReturnAddrResolutionFunc() {
  return nullptr;
}

AddDynamicSymbol GetProfilerAddDynamicSymbolFunc() {
  return nullptr;
}

MoveDynamicSymbol GetProfilerMoveDynamicSymbolFunc() {
  return nullptr;
}


}  // namespace base::debug
