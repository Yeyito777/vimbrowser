// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/password_store/password_store_backend_metrics_recorder.h"

#include <utility>

#include "base/metrics/histogram_functions.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"

namespace password_manager {

namespace {
constexpr char kMetricPrefix[] = "PasswordManager.PasswordStore";

bool HasRunToCompletion(
    PasswordStoreBackendMetricsRecorder::SuccessStatus success_status) {
  switch (success_status) {
    case PasswordStoreBackendMetricsRecorder::SuccessStatus::kSuccess:
    case PasswordStoreBackendMetricsRecorder::SuccessStatus::kError:
      return true;
    case PasswordStoreBackendMetricsRecorder::SuccessStatus::kCancelledTimeout:
    case PasswordStoreBackendMetricsRecorder::SuccessStatus::
        kCancelledPwdSyncStateChanged:
      return false;
  }
  NOTREACHED();
}

}  // namespace

PasswordStoreBackendMetricsRecorder::PasswordStoreBackendMetricsRecorder() =
    default;

PasswordStoreBackendMetricsRecorder::PasswordStoreBackendMetricsRecorder(
    BackendInfix backend_infix,
    MethodName method_name,
    PasswordStoreAndroidBackendType store_type)
    : backend_infix_(std::move(backend_infix)),
      method_name_(std::move(method_name)),
      store_type_(store_type) {
  RecordRequestStatus(StoreBackendRequestStatus::kRequestIssued);
}

PasswordStoreBackendMetricsRecorder::PasswordStoreBackendMetricsRecorder(
    PasswordStoreBackendMetricsRecorder&&) = default;

PasswordStoreBackendMetricsRecorder& PasswordStoreBackendMetricsRecorder::
    PasswordStoreBackendMetricsRecorder::operator=(
        PasswordStoreBackendMetricsRecorder&&) = default;

PasswordStoreBackendMetricsRecorder::~PasswordStoreBackendMetricsRecorder() =
    default;

void PasswordStoreBackendMetricsRecorder::RecordMetrics(
    SuccessStatus success_status,
    std::optional<PasswordStoreBackendError> /*error*/) const {
  RecordSuccess(success_status);
  if (HasRunToCompletion(success_status)) {
    RecordLatency();
    RecordRequestStatus(StoreBackendRequestStatus::kCompleted);
  } else if (success_status == SuccessStatus::kCancelledTimeout) {
    RecordRequestStatus(StoreBackendRequestStatus::kTimeout);
  } else if (success_status == SuccessStatus::kCancelledPwdSyncStateChanged) {
    RecordRequestStatus(
        StoreBackendRequestStatus::kCancelledPwdSyncStateChanged);
  }
}

base::TimeDelta
PasswordStoreBackendMetricsRecorder::GetElapsedTimeSinceCreation() const {
  return base::Time::Now() - start_;
}

void PasswordStoreBackendMetricsRecorder::RecordRequestStatus(
    StoreBackendRequestStatus request_status) const {
  // Infixes for the overall and backend specific histogram.
  std::vector<std::string> possible_infixes = {"Backend", *backend_infix_};
  // Adding the infix for split stores.
  if (store_type_ != PasswordStoreAndroidBackendType::kNone) {
    possible_infixes.push_back(GetStoreInfix());
  }

  for (const auto& infix : possible_infixes) {
    base::UmaHistogramEnumeration(
        base::JoinString({base::StrCat({kMetricPrefix, infix}), *method_name_},
                         "."),
        request_status);
  }
}

void PasswordStoreBackendMetricsRecorder::RecordSuccess(
    SuccessStatus success_status) const {
  // Infixes for the overall and backend specific histogram.
  std::vector<std::string> possible_infixes = {"Backend", *backend_infix_};
  // Adding the infix for split stores.
  if (store_type_ != PasswordStoreAndroidBackendType::kNone) {
    possible_infixes.push_back(GetStoreInfix());

    base::UmaHistogramBoolean(
        base::JoinString(
            {base::StrCat({kMetricPrefix, GetStoreInfix()}), "Success"}, "."),
        success_status == SuccessStatus::kSuccess);
  }

  for (const auto& infix : possible_infixes) {
    base::UmaHistogramBoolean(
        base::JoinString(
            {base::StrCat({kMetricPrefix, infix}), *method_name_, "Success"},
            "."),
        success_status == SuccessStatus::kSuccess);
  }
}

void PasswordStoreBackendMetricsRecorder::RecordLatency() const {
  base::TimeDelta duration = GetElapsedTimeSinceCreation();

  // Infixes for the overall and backend specific histogram.
  std::vector<std::string> possible_infixes = {"Backend", *backend_infix_};
  // Adding the infix for split stores.
  if (store_type_ != PasswordStoreAndroidBackendType::kNone) {
    possible_infixes.push_back(GetStoreInfix());
  }

  for (const auto& infix : possible_infixes) {
    base::UmaHistogramMediumTimes(
        base::JoinString(
            {base::StrCat({kMetricPrefix, infix}), *method_name_, "Latency"},
            "."),
        duration);
  }
}

std::string PasswordStoreBackendMetricsRecorder::GetStoreInfix() const {
  switch (store_type_) {
    case PasswordStoreBackendMetricsRecorder::PasswordStoreAndroidBackendType::
        kAccount:
      return base::JoinString({*backend_infix_, "Account"}, ".");
    case PasswordStoreBackendMetricsRecorder::PasswordStoreAndroidBackendType::
        kLocal:
      return base::JoinString({*backend_infix_, "Local"}, ".");
    default:
      NOTREACHED();
  }
}

}  // namespace password_manager
