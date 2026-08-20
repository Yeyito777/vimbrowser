// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/tracing_delegate.h"

#include "base/functional/bind.h"
#include "components/tracing/common/background_tracing_state_manager.h"


namespace content {

bool TracingDelegate::IsRecordingAllowed(bool requires_anonymized_data,
                                         base::TimeTicks session_start) const {
  return true;
}

bool TracingDelegate::ShouldSaveUnuploadedTrace() const {
  return true;
}

std::unique_ptr<tracing::BackgroundTracingStateManager>
TracingDelegate::CreateStateManager() {
  return nullptr;
}

std::string TracingDelegate::RecordSerializedSystemProfileMetrics() const {
  return std::string();
}

tracing::MetadataDataSource::BundleRecorder
TracingDelegate::CreateSystemProfileMetadataRecorder() const {
  return base::BindRepeating(
      &tracing::MetadataDataSource::RecordDefaultBundleMetadata);
}


}  // namespace content
