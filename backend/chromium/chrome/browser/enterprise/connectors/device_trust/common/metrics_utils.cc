// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/connectors/device_trust/common/metrics_utils.h"

#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "chrome/browser/enterprise/connectors/device_trust/common/common_types.h"


namespace enterprise_connectors {

namespace {


DTHandshakeResult ResponseToResult(const DeviceTrustResponse& response) {
  if (!response.error) {
    return DTHandshakeResult::kSuccess;
  }

  switch (response.error.value()) {
    case DeviceTrustError::kUnknown:
      return DTHandshakeResult::kUnknown;
    case DeviceTrustError::kTimeout:
      return DTHandshakeResult::kTimeout;
    case DeviceTrustError::kFailedToParseChallenge:
      return DTHandshakeResult::kFailedToParseChallenge;
    case DeviceTrustError::kFailedToCreateResponse:
      return DTHandshakeResult::kFailedToCreateResponse;
  }
}

bool ContainsPolicyLevel(const std::set<DTCPolicyLevel>& levels,
                         const DTCPolicyLevel& level) {
  return levels.find(level) != levels.end();
}

DTAttestationPolicyLevel GetAttestationPolicyLevel(
    const std::set<DTCPolicyLevel>& levels) {
  if (levels.empty()) {
    return DTAttestationPolicyLevel::kNone;
  }

  if (ContainsPolicyLevel(levels, DTCPolicyLevel::kBrowser)) {
    if (ContainsPolicyLevel(levels, DTCPolicyLevel::kUser)) {
      return DTAttestationPolicyLevel::kUserAndBrowser;
    }
    return DTAttestationPolicyLevel::kBrowser;
  }

  if (ContainsPolicyLevel(levels, DTCPolicyLevel::kUser)) {
    return DTAttestationPolicyLevel::kUser;
  }

  return DTAttestationPolicyLevel::kUnknown;
}

}  // namespace

void LogAttestationFunnelStep(DTAttestationFunnelStep step) {
  static constexpr char kFunnelStepHistogram[] =
      "Enterprise.DeviceTrust.Attestation.Funnel";
  base::UmaHistogramEnumeration(kFunnelStepHistogram, step);
  VLOG(1) << "Device Trust attestation step: " << static_cast<int>(step);
}

void LogAttestationPolicyLevel(const std::set<DTCPolicyLevel>& levels) {
  static constexpr char kAttestationPolicyLevelHistogram[] =
      "Enterprise.DeviceTrust.Attestation.PolicyLevel";
  base::UmaHistogramEnumeration(kAttestationPolicyLevelHistogram,
                                GetAttestationPolicyLevel(levels));
}

void LogAttestationResult(DTAttestationResult result) {
  static constexpr char kAttestationResultHistogram[] =
      "Enterprise.DeviceTrust.Attestation.Result";
  base::UmaHistogramEnumeration(kAttestationResultHistogram, result);
  if (!IsSuccessAttestationResult(result)) {
    LOG(ERROR) << "Device Trust attestation error: "
               << AttestationErrorToString(result);
  }
}

void LogDeviceTrustResponse(const DeviceTrustResponse& response,
                            base::TimeTicks start_time) {
  static constexpr char kLatencyHistogramFormat[] =
      "Enterprise.DeviceTrust.Attestation.ResponseLatency.%s";
  base::UmaHistogramTimes(
      base::StringPrintf(kLatencyHistogramFormat,
                         response.error ? "Failure" : "Success"),
      base::TimeTicks::Now() - start_time);

  static constexpr char kHandshakeResultHistogram[] =
      "Enterprise.DeviceTrust.Handshake.Result";
  base::UmaHistogramEnumeration(kHandshakeResultHistogram,
                                ResponseToResult(response));
}


}  // namespace enterprise_connectors
