// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

if (globalThis.lcpObserver === undefined) {
  globalThis.lcpObserver = new PerformanceObserver((entryList) => {
    globalThis.largestContentfulPaint = true;
    delete globalThis.lcpObserver;
  });
  globalThis.lcpObserver.observe({
    type: 'largest-contentful-paint',
    buffered: true,
  });
}
