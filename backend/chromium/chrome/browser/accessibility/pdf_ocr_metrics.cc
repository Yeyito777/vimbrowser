// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/accessibility/pdf_ocr_metrics.h"

#include "base/metrics/histogram_macros.h"
#include "ui/accessibility/platform/ax_platform.h"


namespace accessibility {

void RecordPDFOpenedWithA11yFeatureWithPdfOcr() {
  bool is_pdf_ocr_on = true;

  if (ui::AXPlatform::GetInstance().IsScreenReaderActive()) {
    UMA_HISTOGRAM_BOOLEAN("Accessibility.PDF.OpenedWithScreenReader.PdfOcr",
                          is_pdf_ocr_on);
  }

}

}  // namespace accessibility
