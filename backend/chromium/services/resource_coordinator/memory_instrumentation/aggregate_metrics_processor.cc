// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/resource_coordinator/memory_instrumentation/aggregate_metrics_processor.h"

namespace memory_instrumentation {

mojom::AggregatedMetricsPtr ComputeGlobalNativeCodeResidentMemoryKb(
    const std::map<base::ProcessId, mojom::RawOSMemDump*>&) {
  return mojom::AggregatedMetrics::New();
}

}  // namespace memory_instrumentation
