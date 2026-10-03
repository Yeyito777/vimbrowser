// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/handwriting/handwriting_recognition_service_factory.h"

#include <utility>

#include "build/build_config.h"
#include "build/buildflag.h"

// The default service which does not have any real handwriting recognition
// backend.
#include "content/browser/handwriting/handwriting_recognition_service_impl.h"

namespace content {

void CreateHandwritingRecognitionService(
    RenderFrameHost*,  // Required for BinderMapWithContext interface.
    mojo::PendingReceiver<handwriting::mojom::HandwritingRecognitionService>
        pending_receiver) {
  HandwritingRecognitionServiceImpl::Create(std::move(pending_receiver));
}

}  // namespace content
