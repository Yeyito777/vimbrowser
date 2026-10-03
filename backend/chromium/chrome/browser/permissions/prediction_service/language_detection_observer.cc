// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include "chrome/browser/permissions/prediction_service/language_detection_observer.h"

namespace permissions {

LanguageDetectionObserver::LanguageDetectionObserver() = default;

LanguageDetectionObserver::~LanguageDetectionObserver() = default;

void LanguageDetectionObserver::Init(
    content::WebContents*,
    base::OnceCallback<void()>,
    base::OnceCallback<void()> on_fallback) {
  std::move(on_fallback).Run();
}

void LanguageDetectionObserver::Reset() {}

bool LanguageDetectionObserver::WaitingForLanguageDetection() {
  return false;
}
}  // namespace permissions
