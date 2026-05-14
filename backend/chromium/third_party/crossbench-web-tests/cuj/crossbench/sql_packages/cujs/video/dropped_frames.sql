-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
DROP TABLE IF EXISTS dropped_frames;

CREATE PERFETTO TABLE dropped_frames AS
SELECT
  (
    SELECT
      CAST(args.display_value AS REAL)
    FROM slice
    JOIN args
      ON slice.arg_set_id = args.arg_set_id
    WHERE
      slice.name = 'dropped-frames-percent'
      AND slice.cat = 'blink.user_timing'
      AND args.key = 'debug.data.detail'
  ) AS dropped_frames_percent,
  (
    SELECT
      CAST(args.display_value AS INTEGER)
    FROM slice
    JOIN args
      ON slice.arg_set_id = args.arg_set_id
    WHERE
      slice.name = 'dropped-frames-count'
      AND slice.cat = 'blink.user_timing'
      AND args.key = 'debug.data.detail'
  ) AS dropped_frames_count;
