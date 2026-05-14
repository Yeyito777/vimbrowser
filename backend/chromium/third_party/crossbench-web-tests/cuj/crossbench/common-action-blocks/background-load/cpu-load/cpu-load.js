// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const workers = [];
window.cpu_load_workers = workers;
const params = new URLSearchParams(window.location.search);

const constantString = params.get('constant');
const constant = constantString === null ? 1 : Number.parseInt(constantString);
const scaleString = params.get('scale');
const scale = scaleString === null ? 0.0 : Number.parseFloat(scaleString);

const concurrency = Math.floor(
    navigator.hardwareConcurrency * scale + constant,
);
console.log(`concurrency: ${concurrency}`);
for (let i = 0; i < concurrency; i++) {
  const worker = new Worker('./cpu-load-worker.js');
  worker.postMessage(i);
  workers.push(worker);
}
