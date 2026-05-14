// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const expectedNumClicks = EXPECTED_NUM_CLICKS;
const countDisplay = document.getElementById('count');
const actualCount = parseInt(countDisplay.textContent);

if (actualCount !== expectedNumClicks) {
  throw new Error(
      `Expected ${expectedNumClicks} clicks, but found ${actualCount}.`,
  );
}
