// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/ipc/common/device_perf_info_mojom_traits.h"

#include "base/notreached.h"
#include "build/build_config.h"

namespace mojo {


gpu::mojom::HasDiscreteGpu
EnumTraits<gpu::mojom::HasDiscreteGpu, gpu::HasDiscreteGpu>::ToMojom(
    gpu::HasDiscreteGpu has_discrete_gpu) {
  switch (has_discrete_gpu) {
    case gpu::HasDiscreteGpu::kUnknown:
      return gpu::mojom::HasDiscreteGpu::kUnknown;
    case gpu::HasDiscreteGpu::kNo:
      return gpu::mojom::HasDiscreteGpu::kNo;
    case gpu::HasDiscreteGpu::kYes:
      return gpu::mojom::HasDiscreteGpu::kYes;
  }
  NOTREACHED() << "Invalid gpu::HasDiscreteGpu: "
               << static_cast<int>(has_discrete_gpu);
}

// static
bool EnumTraits<gpu::mojom::HasDiscreteGpu, gpu::HasDiscreteGpu>::FromMojom(
    gpu::mojom::HasDiscreteGpu input,
    gpu::HasDiscreteGpu* out) {
  switch (input) {
    case gpu::mojom::HasDiscreteGpu::kUnknown:
      *out = gpu::HasDiscreteGpu::kUnknown;
      return true;
    case gpu::mojom::HasDiscreteGpu::kNo:
      *out = gpu::HasDiscreteGpu::kNo;
      return true;
    case gpu::mojom::HasDiscreteGpu::kYes:
      *out = gpu::HasDiscreteGpu::kYes;
      return true;
  }
  NOTREACHED() << "Invalid gpu::mojom::HasDiscreteGpu: " << input;
}

// static
bool StructTraits<gpu::mojom::DevicePerfInfoDataView, gpu::DevicePerfInfo>::
    Read(gpu::mojom::DevicePerfInfoDataView data, gpu::DevicePerfInfo* out) {
  out->total_physical_memory_mb = data.total_physical_memory_mb();
  out->total_disk_space_mb = data.total_disk_space_mb();
  out->hardware_concurrency = data.hardware_concurrency();
  bool rt = true;
  return rt;
}

}  // namespace mojo
