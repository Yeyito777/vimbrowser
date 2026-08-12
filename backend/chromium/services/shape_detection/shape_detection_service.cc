// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/shape_detection/shape_detection_service.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "services/shape_detection/text_detection_impl.h"


#if BUILDFLAG(IS_MAC)
#include "services/shape_detection/barcode_detection_provider_mac.h"
#elif BUILDFLAG(GOOGLE_CHROME_BRANDING) &&  (BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_WIN))
#include "services/shape_detection/barcode_detection_provider_chrome.h"
#else
#include "services/shape_detection/barcode_detection_provider_impl.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "services/shape_detection/face_detection_provider_win.h"
#elif BUILDFLAG(IS_MAC)
#include "services/shape_detection/face_detection_provider_mac.h"
#else
#include "services/shape_detection/face_detection_provider_impl.h"
#endif

namespace shape_detection {

ShapeDetectionService::ShapeDetectionService(
    mojo::PendingReceiver<mojom::ShapeDetectionService> receiver)
    : receiver_(this, std::move(receiver)) {
}

ShapeDetectionService::~ShapeDetectionService() = default;

void ShapeDetectionService::BindBarcodeDetectionProvider(
    mojo::PendingReceiver<mojom::BarcodeDetectionProvider> receiver) {
#if BUILDFLAG(IS_MAC)
  BarcodeDetectionProviderMac::Create(std::move(receiver));
#elif BUILDFLAG(GOOGLE_CHROME_BRANDING) &&  (BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX))
  BarcodeDetectionProviderChrome::Create(std::move(receiver));
#else
  BarcodeDetectionProviderImpl::Create(std::move(receiver));
#endif
}

void ShapeDetectionService::BindFaceDetectionProvider(
    mojo::PendingReceiver<mojom::FaceDetectionProvider> receiver) {
#if BUILDFLAG(IS_MAC)
  FaceDetectionProviderMac::Create(std::move(receiver));
#elif BUILDFLAG(IS_WIN)
  FaceDetectionProviderWin::Create(std::move(receiver));
#else
  FaceDetectionProviderImpl::Create(std::move(receiver));
#endif
}

void ShapeDetectionService::BindTextDetection(
    mojo::PendingReceiver<mojom::TextDetection> receiver) {
  TextDetectionImpl::Create(std::move(receiver));
}

}  // namespace shape_detection
