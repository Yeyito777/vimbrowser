-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
DROP TABLE IF EXISTS video_seek_output;

CREATE PERFETTO TABLE video_seek_output AS
WITH
  averageseektime AS (
    SELECT
      -- Convert nanoseconds to seconds
      (
        sum(dur) / count(name)
      ) / 1000000000.0 AS avg_seek_time_s
    FROM slice
    WHERE
      name GLOB 'randomSeek-*-duration'
  ),
  targetseeks AS (
    SELECT
      CAST(extract_arg(arg_set_id, 'debug.data.detail') -> '$.numTargetSeeks' AS REAL) AS num_target_seeks
    FROM slice AS s
    WHERE
      s.name = 'test-config'
    -- Assuming only one 'test-config' mark with this detail
    LIMIT 1
  ),
  completedseeks AS (
    SELECT
      count(name) AS completed_count
    FROM slice
    WHERE
      name GLOB 'randomSeek-*-duration'
  )
SELECT
  ast.avg_seek_time_s,
  (
    cs.completed_count * 100.0 / ts.num_target_seeks
  ) AS seek_completion_percentage
FROM averageseektime AS ast, targetseeks AS ts, completedseeks AS cs;
