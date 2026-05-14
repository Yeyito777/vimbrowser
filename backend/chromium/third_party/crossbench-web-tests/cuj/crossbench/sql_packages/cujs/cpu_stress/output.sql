-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
DROP VIEW IF EXISTS all_started;

CREATE VIEW all_started AS
-- The last of the first compress events
SELECT
  max(ts) AS ts,
  count(*) AS workers
FROM (
  -- The first compress event on every track
  SELECT
    track_id,
    min(ts) AS ts
  FROM slice
  WHERE
    category = 'blink.user_timing' AND name = 'compress'
  GROUP BY
    track_id
);

DROP TABLE IF EXISTS cpu_stress_output;

CREATE PERFETTO TABLE cpu_stress_output AS
SELECT
  sum(CASE WHEN ever_hidden = 0 THEN size END) / 1048576.0 AS foreground_size,
  avg(CASE WHEN ever_hidden = 0 THEN size / duration END) / 1048576.0 AS foreground_throughput,
  sum(CASE WHEN ever_hidden = 1 THEN size END) / 1048576.0 AS background_size,
  avg(CASE WHEN ever_hidden = 1 THEN size / duration END) / 1048576.0 AS background_throughput,
  min(workers) AS workers
FROM (
  SELECT
    detail -> '$.everHidden' AS ever_hidden,
    detail -> '$.size' AS "size",
    detail -> '$.time' AS duration,
    workers
  FROM (
    SELECT
      extract_arg(arg_set_id, 'debug.data.detail') AS detail,
      all_started.workers AS workers
    FROM slice, all_started
    WHERE
      slice.ts > all_started.ts AND category = 'blink.user_timing' AND name = 'compress'
  )
);
