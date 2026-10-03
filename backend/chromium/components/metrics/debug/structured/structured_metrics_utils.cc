// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/debug/structured/structured_metrics_utils.h"

#include "base/i18n/number_formatting.h"
#include "base/strings/string_number_conversions.h"
#include "components/metrics/structured/structured_metrics_service.h"

namespace metrics::structured {

base::Value GetStructuredMetricsSummary(StructuredMetricsService* service) {
  base::DictValue result = base::DictValue().Set("enabled", false);


  if (service && service->recording_enabled()) {
    result.Set("enabled", true);
  }

  return base::Value(std::move(result));
}

}  // namespace metrics::structured
