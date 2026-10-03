// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/performance_manager/mechanisms/working_set_trimmer.h"

#include "base/no_destructor.h"
#include "build/build_config.h"


namespace performance_manager {
namespace mechanism {
namespace {

// The NoOpWorkingSetTrimmer provides an implementation of a working set trimmer
// that does nothing on unsupported platforms.
class NoOpWorkingSetTrimmer : public WorkingSetTrimmer {
 public:
  ~NoOpWorkingSetTrimmer() override = default;
  NoOpWorkingSetTrimmer() = default;

  // WorkingSetTrimmer implementation:
  bool PlatformSupportsWorkingSetTrim() override { return false; }
  void TrimWorkingSet(const ProcessNode* node) override {}
};

}  // namespace

WorkingSetTrimmer* WorkingSetTrimmer::GetInstance() {
  static base::NoDestructor<NoOpWorkingSetTrimmer> trimmer;
  return trimmer.get();
}

}  // namespace mechanism
}  // namespace performance_manager
