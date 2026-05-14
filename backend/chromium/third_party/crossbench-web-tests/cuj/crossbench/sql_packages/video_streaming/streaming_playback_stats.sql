-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
DROP TABLE IF EXISTS streaming_stats_output;

CREATE PERFETTO TABLE streaming_stats_output AS
SELECT
  CAST(extract_arg(arg_set_id, 'debug.data.detail') -> '$.currentTime' AS REAL) AS playback_duration,
  CAST(extract_arg(arg_set_id, 'debug.data.detail') -> '$.videoWidth' AS INTEGER) AS playback_resolution_width,
  CAST(extract_arg(arg_set_id, 'debug.data.detail') -> '$.videoHeight' AS INTEGER) AS playback_resolution_height,
  CAST(extract_arg(arg_set_id, 'debug.data.detail') -> '$.totalVideoFrames' AS INTEGER) AS playback_total_frames,
  CAST(extract_arg(arg_set_id, 'debug.data.detail') -> '$.droppedVideoFrames' AS INTEGER) AS playback_dropped_frames
FROM slice
WHERE
  name = 'streaming-stats' AND category = 'blink.user_timing';
