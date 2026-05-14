// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
const downloadUrl = 'https://speed.cloudflare.com/__down?bytes=1000000';
const uploadUrl = 'https://speed.cloudflare.com/__up';
const iterations = 10;

// DOWNLOAD
let downTotalMbps = 0;
let downTotalLatency = 0;

for (let i = 0; i < iterations; i++) {
  const start = performance.now();

  const response = await fetch(downloadUrl, {cache: 'no-store'});

  const headersReceived = performance.now();
  const latency = headersReceived - start;

  if (!response.ok) throw new Error(`Status ${response.status}`);

  const blob = await response.blob();
  const end = performance.now();

  const downloadDurationSec = (end - headersReceived) / 1000;
  const sizeInBits = blob.size * 8;
  const mbps = sizeInBits / downloadDurationSec / 1_000_000;

  downTotalMbps += mbps;
  downTotalLatency += latency;
}

const downAvgMbps = downTotalMbps / iterations;
const downAvgLatency = downTotalLatency / iterations;

// UPLOAD
const fileSizeMB = 1;
const fileSizeBytes = fileSizeMB * 1024 * 1024;

function fillRandomBytes(buffer) {
  const MAX_BYTES = 65536;
  let offset = 0;
  while (offset < buffer.length) {
    const count = Math.min(MAX_BYTES, buffer.length - offset);
    const view = buffer.subarray(offset, offset + count);
    crypto.getRandomValues(view);
    offset += count;
  }
  return buffer;
}

const payloadBuffer = new Uint8Array(fileSizeBytes);
fillRandomBytes(payloadBuffer);
const payloadBlob = new Blob([payloadBuffer]);

let upTotalMbps = 0;

for (let i = 0; i < iterations; i++) {
  const start = performance.now();

  const response = await fetch(uploadUrl, {
    method: 'POST',
    body: payloadBlob,
    cache: 'no-store',
  });

  const end = performance.now();

  if (!response.ok) throw new Error(`Status ${response.status}`);

  const durationSeconds = (end - start) / 1000;
  const sizeInBits = fileSizeBytes * 8;
  const mbps = sizeInBits / durationSeconds / 1_000_000;

  upTotalMbps += mbps;
}

const upAvgMbps = upTotalMbps / iterations;

// RESULTS
performance.mark('speed-test-result', {
  detail: {
    download: downAvgMbps,
    upload: upAvgMbps,
    latency: downAvgLatency,
  },
});
