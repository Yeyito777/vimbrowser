// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const verify_hw_encode = VERIFY_HW_ENCODE
const verify_hw_decode = VERIFY_HW_DECODE

function assert(condition, message) {
    if (!condition) {
        throw new Error(message || "Assertion failed");
    }
}

const rtcResult = {}

try {
    const localPc = testVisible.localPeerConnections[0];
    assert(localPc, "Local PeerConnection not found");
    const localStats = await localPc.getStats(null);
    localStats.forEach(report => {
        if (report.type === 'codec') {
            const mimeType = report.mimeType
            for (const codec of ["H264", "VP8", "VP9", "AV1"]) {
                if (mimeType.includes(codec)) {
                    rtcResult.codecType = codec;
                    break;
                }
            }
        }
        if (report.type === 'outbound-rtp' && report.kind === 'video') {
            rtcResult.encoder = {};
            rtcResult.encoder.encoder = report.encoderImplementation;
            rtcResult.encoder.powerEfficient = report.powerEfficientEncoder;
            rtcResult.encoder.numEncodedFrames = report.framesEncoded;
        }
    });

    const remotePc = testVisible.remotePeerConnections[0];
    assert(remotePc, "Remote PeerConnection not found");
    const remoteStats = await remotePc.getStats(null);

    remoteStats.forEach(report => {
        if (report.type === 'inbound-rtp' && report.kind === 'video') {
            rtcResult.decoder = {};
            rtcResult.decoder.decoder = report.decoderImplementation;
            rtcResult.decoder.powerEfficient = report.powerEfficientDecoder;
            rtcResult.decoder.numDecodedFrames = report.framesDecoded;
        }
    });
} catch (error) {
    assert(false, `Failed to get codec information: ${error}`)
}


assert(rtcResult.encoder.numEncodedFrames > 0, "No frames are encoded");
assert(rtcResult.decoder.numDecodedFrames > 0, "No frames are decoded");

if (verify_hw_encode){
  assert(rtcResult.encoder.powerEfficient, "Expected HW encoder used, but SW encoder was used instead")
}

if (verify_hw_decode){
  assert(rtcResult.decoder.powerEfficient, "Expected HW decoder used, but SW decoder was used instead")
}
