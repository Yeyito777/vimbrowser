// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/performance_manager/mechanisms/termination_target_setter.h"

#include "base/process/process.h"
#include "components/performance_manager/public/graph/process_node.h"
#include "partition_alloc/page_allocator.h"


namespace performance_manager {

void TerminationTargetSetter::SetTerminationTarget(
    const ProcessNode* process_node) {
}

}  // namespace performance_manager
