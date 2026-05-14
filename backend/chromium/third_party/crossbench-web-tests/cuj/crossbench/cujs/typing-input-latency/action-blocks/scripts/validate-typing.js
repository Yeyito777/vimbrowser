// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const expectedNumCharacters = EXPECTED_CHARACTERS;
const countDisplay = document.getElementById('charCount');
const actualCount = parseInt(countDisplay.textContent);

if (actualCount !== expectedNumCharacters) {
  throw new Error(
      `Expected ${expectedNumCharacters} characters, but found ${actualCount}.`,
  );
}
