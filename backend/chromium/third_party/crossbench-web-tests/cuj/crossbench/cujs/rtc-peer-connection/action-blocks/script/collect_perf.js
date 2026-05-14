// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Throws an error if the provided condition is not met.
 * @param {boolean} condition The condition to check.
 * @param {string} message The error message to throw if the condition is false.
 */
function assert(condition, message) {
  if (!condition) {
    throw new Error(message || 'Assertion failed');
  }
}

/**
 * Gets the relevant video stats report from a getStats() RTCStatsReport object.
 * @param {RTCStatsReport} statsReport The report from which to extract stats.
 * @param {string} type The report type to find ('inbound-rtp' or 'outbound-rtp').
 * @return {object} The specific stats object for the video stream.
 */
function getVideoStats(statsReport, type) {
  let videoStats;
  statsReport.forEach(report => {
    if (report.type === type && report.kind === 'video') {
      videoStats = report;
    }
  });
  return videoStats;
}


/**
 * Collects and analyzes the end-to-end performance of a WebRTC video stream
 * over a single 10-second interval. It reports the final metrics using
 * `performance.mark()`.
 *
 * @return {Promise<void>} A promise that resolves when all metrics have been reported.
 */
async function analyzeStreamPerformanceAndReport() {
  const localPc = testVisible.localPeerConnections[0];
  const remotePc = testVisible.remotePeerConnections[0];
  const measurementDuration = 10000; // 10 seconds

  const [initialLocalStats, initialRemoteStats] = await Promise.all([
    localPc.getStats(null),
    remotePc.getStats(null),
  ]);

  const initialTx = getVideoStats(initialLocalStats, 'outbound-rtp');
  const initialRx = getVideoStats(initialRemoteStats, 'inbound-rtp');

  assert(initialTx, 'Could not find initial outbound-rtp video stats.');
  assert(initialRx, 'Could not find initial inbound-rtp video stats.');

  await new Promise(resolve => setTimeout(resolve, measurementDuration));

  const [finalLocalStats, finalRemoteStats] = await Promise.all([
    localPc.getStats(null),
    remotePc.getStats(null),
  ]);

  const finalTx = getVideoStats(finalLocalStats, 'outbound-rtp');
  const finalRx = getVideoStats(finalRemoteStats, 'inbound-rtp');

  assert(finalTx, 'Could not find final outbound-rtp video stats.');
  assert(finalRx, 'Could not find final inbound-rtp video stats.');

  const framesEncodedDiff = finalTx.framesEncoded - initialTx.framesEncoded;
  assert(framesEncodedDiff > 0, `Frames encoded must be positive. Got ${framesEncodedDiff}.`);
  const encodeTime = (finalTx.totalEncodeTime - initialTx.totalEncodeTime) * 1000 / framesEncodedDiff;

  const framesDecodedDiff = finalRx.framesDecoded - initialRx.framesDecoded;
  assert(framesDecodedDiff > 0, `Frames decoded must be positive. Got ${framesDecodedDiff}.`);
  const decodeTime = (finalRx.totalDecodeTime - initialRx.totalDecodeTime) * 1000 / framesDecodedDiff;
  const droppedFramesRatio = (finalRx.framesDropped - initialRx.framesDropped) / framesDecodedDiff;

  const fps = finalTx.framesPerSecond;

  const perfResult = {
    fps: fps,
    encode_time_ms: encodeTime,
    decode_time_ms: decodeTime,
    dropped_frames_ratio: droppedFramesRatio,
  };

  performance.mark('rtc-perf', {
    detail: {
      value: perfResult,
    },
  });
}

await analyzeStreamPerformanceAndReport();