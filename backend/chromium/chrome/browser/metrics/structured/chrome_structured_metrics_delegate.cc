// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/metrics/structured/chrome_structured_metrics_delegate.h"

#include <stdint.h>

#include <utility>

#include "base/no_destructor.h"
#include "components/metrics/structured/recorder.h"
#include "components/metrics/structured/structured_metrics_client.h"
#include "components/metrics/structured/structured_metrics_features.h"
#include "components/metrics_services_manager/metrics_services_manager.h"


namespace metrics::structured {
namespace {

// Platforms for which the StructuredMetricsClient will be initialized for.
enum class StructuredMetricsPlatform {
  kUninitialized = 0,
  kAshChrome = 1,
};


class DefaultDelegate : public RecordingDelegate {
 public:
  DefaultDelegate() = default;

  DefaultDelegate(const DefaultDelegate&) = delete;
  DefaultDelegate& operator=(const DefaultDelegate&) = delete;

  ~DefaultDelegate() override = default;

  // RecordingDelegate:
  void RecordEvent(Event&& event) override {
    Recorder::GetInstance()->RecordEvent(std::move(event));
  }

  bool IsReadyToRecord() const override { return true; }
};

}  // namespace

ChromeStructuredMetricsDelegate::ChromeStructuredMetricsDelegate() {
// TODO(jongahn): Make a static factory class and pass it into ctor.
  delegate_ = std::make_unique<DefaultDelegate>();
  StructuredMetricsClient::Get()->SetDelegate(this);
}

ChromeStructuredMetricsDelegate::~ChromeStructuredMetricsDelegate() = default;

// static
ChromeStructuredMetricsDelegate* ChromeStructuredMetricsDelegate::Get() {
  static base::NoDestructor<ChromeStructuredMetricsDelegate> chrome_recorder;
  return chrome_recorder.get();
}

void ChromeStructuredMetricsDelegate::Initialize() {
  // Windows, Mac, and Linux do not have initialization events due to DMA
  // concerns.

  is_initialized_ = true;
}

void ChromeStructuredMetricsDelegate::RecordEvent(Event&& event) {
  DCHECK(IsReadyToRecord());
  delegate_->RecordEvent(std::move(event));
}

bool ChromeStructuredMetricsDelegate::IsReadyToRecord() const {
  return delegate_->IsReadyToRecord();
}

}  // namespace metrics::structured
