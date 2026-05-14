-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE sql_packages.web_tests_common.iterations;

DROP VIEW IF EXISTS meminfo;

CREATE VIEW meminfo AS
SELECT
  ts,
  extract_arg(arg_set_id, 'debug.data.detail') AS "json"
FROM slice
WHERE
  category = 'blink.user_timing' AND name = 'crossbench-meminfo';

DROP VIEW IF EXISTS meminfo_with_iteration;

CREATE VIEW meminfo_with_iteration AS
SELECT
  iterations.id AS it_id,
  ts,
  "json"
FROM iterations
JOIN meminfo
  ON meminfo.ts >= iterations.start AND meminfo.ts <= iterations.end;

DROP TABLE IF EXISTS meminfo_total_output;

CREATE PERFETTO TABLE meminfo_total_output AS
SELECT
  -- crossbench-meminfo event's detail field is a JSON blob of type:
  --  {
  --    title: 'title'
  --    processes: [
  --      {
  --        pid: number
  --        pss_total: number
  --        rss_total: number
  --        swap_total: number
  --      },
  --      ...
  --    ]
  --    system: {
  --      total_ram_kb: number
  --      free_kb: number
  --      dma_buf_kb: number
  --    }
  --  }
  -- We have a row per process per meminfo event, sum up the meminfo counters
  -- for each meminfo event.
  it_id,
  ts,
  title,
  sum(pss_total_kb) / 1024.0 AS pss_total_mb,
  sum(rss_total_kb) / 1024.0 AS rss_total_mb,
  sum(swap_total_kb) / 1024.0 AS swap_total_mb,
  free_kb / 1024.0 AS free_mb,
  dma_buf_kb / 1024.0 AS dma_buf_mb
FROM (
  -- Extract the per-process objects and join them to their meminfo rows, to get
  -- a row per process per meminfo event.
  SELECT
    meminfo_with_iteration.it_id AS it_id,
    meminfo_with_iteration.ts AS ts,
    meminfo_with_iteration.json -> '$.title' AS title,
    CAST(per_process_meminfo_json.value -> '$.pss_total' AS REAL) AS pss_total_kb,
    CAST(per_process_meminfo_json.value -> '$.rss_total' AS REAL) AS rss_total_kb,
    CAST(per_process_meminfo_json.value -> '$.swap_total' AS REAL) AS swap_total_kb,
    CAST(meminfo_with_iteration.json -> '$.system.free_kb' AS REAL) AS free_kb,
    CAST(meminfo_with_iteration.json -> '$.system.dma_buf_kb' AS REAL) AS dma_buf_kb
  FROM meminfo_with_iteration, json_each(meminfo_with_iteration.json -> '$.processes') AS per_process_meminfo_json
)
GROUP BY
  it_id,
  ts,
  title,
  free_kb,
  dma_buf_kb;
