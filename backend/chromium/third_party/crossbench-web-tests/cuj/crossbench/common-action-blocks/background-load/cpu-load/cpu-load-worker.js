// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const spinDurationMs = 1000;

onmessage = (e) => {
  const id = e.data;
  console.log(`starting worker: ${id}`);

  let nextBreak = performance.now() + spinDurationMs;
  function load() {
    let count = 0;
    while (performance.now() < nextBreak) {
      count++;
    }
    nextBreak += spinDurationMs;
    performance.mark('spin', {detail: {id, count}});
    setTimeout(load, 0);
  }

  load();
};
