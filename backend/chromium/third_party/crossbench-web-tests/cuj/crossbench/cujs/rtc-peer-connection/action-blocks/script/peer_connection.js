// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const codec_profile = "CODEC_PROFILE"
const width = RTC_STREAM_WIDTH
const height = RTC_STREAM_HEIGHT
const svc_mode = "SVC_MODE"
const simulcasts = "SIMULCASTS"
const display_media_type = "DISPLAY_MEDIA_TYPE"

/**
 * Checks if a scalabilityMode string represents spatial scalability.
 * For example, "L2T3_KEY" or "S3T1" indicate spatial layers (numLayers > 1),
 * while "L1T2" or an empty string do not.
 * @param {string} scalabilityMode The SVC scalability mode string (e.g., "L2T3_KEY", "L1T2").
 * @returns {boolean} True if the mode represents spatial scalability, false otherwise.
 */
function isSpatialLayerSVC(scalabilityMode) {
  if (scalabilityMode.startsWith("L") || scalabilityMode.startsWith("S")) {
    const numLayersStr = scalabilityMode[1];
    const numLayers = parseInt(numLayersStr);
    if (!isNaN(numLayers)) {
      return numLayers > 1;
    }
  }
  return false;
}

const is_spatial = isSpatialLayerSVC(svc_mode)

if (!is_spatial)
  await start(codec_profile, width, height, simulcasts, svc_mode, display_media_type);
else
  await startSpatialSVC(codec_profile, width, height, svc_mode);
