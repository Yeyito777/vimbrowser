-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.

DROP TABLE IF EXISTS rtc_peer_connection_output;

CREATE PERFETTO TABLE rtc_peer_connection_output AS
SELECT
  CAST(json_extract(extract_arg(arg_set_id, 'debug.data.detail'), '$.value.fps') AS REAL) AS fps,
  CAST(json_extract(extract_arg(arg_set_id, 'debug.data.detail'), '$.value.encode_time_ms') AS REAL) AS encode_time_ms,
  CAST(json_extract(extract_arg(arg_set_id, 'debug.data.detail'), '$.value.decode_time_ms') AS REAL) AS decode_time_ms,
  CAST(json_extract(extract_arg(arg_set_id, 'debug.data.detail'), '$.value.dropped_frames_ratio') AS REAL) AS dropped_frames_ratio
FROM
  slice
WHERE
  name = 'rtc-perf'
  AND cat = 'blink.user_timing';