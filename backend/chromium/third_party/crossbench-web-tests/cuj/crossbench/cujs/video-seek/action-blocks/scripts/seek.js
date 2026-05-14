// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

globalThis.seekCount = 0;
const numSeeks = NUM_SEEK;

console.log(`Starting test, target number of seek: ${numSeeks}`);

performance.mark('test-config', {
  detail: {
    numTargetSeeks: numSeeks,
  },
});

for (let i = 0; i < numSeeks; i++) {
  performance.mark(`randomSeek-${i}-start`);
  console.log(`Starting Seek number: ${i}`);
  try {
    globalThis.seekCount = await randomSeek();
    console.log(`Returned seek count: ${globalThis.seekCount}`);
    performance.mark(`randomSeek-${i}-end`);
    performance.measure(
        `randomSeek-${i}-duration`,
        `randomSeek-${i}-start`,
        `randomSeek-${i}-end`,
    );
  } catch (error) {
    console.error(`\nError while seeking: ${error.message}`);
    console.error(
        `Completed ${globalThis.seekCount}/${numSeeks} seeks before failure.`,
    );
    break;
  }

  // If we've completed all seeks, we can stop early.
  // Note: The count from randomSeek is 0-indexed, so we add 1 for comparison.
  if (globalThis.seekCount + 1 >= numSeeks) {
    console.log(`Early exit: ${globalThis.seekCount}`);
    break;
  }
}
