-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE sql_packages.web_tests_common.iterations;

DROP VIEW IF EXISTS open_latency;

CREATE VIEW open_latency AS
WITH
  page_load_end_events AS (
    SELECT
      substr(name, instr(name, '~') + 1) AS id,
      ts AS page_load_end_ts
    FROM slice
    WHERE
      category = 'blink.user_timing' AND name GLOB 'page-loaded~*'
  ),
  page_load_start_events AS (
    SELECT
      substr(name, instr(name, '~') + 1) AS id,
      ts AS page_load_start_ts
    FROM slice
    WHERE
      category = 'blink.user_timing' AND name GLOB 'page-load~*'
  )
SELECT
  ple.id,
  (
    ple.page_load_end_ts - pls.page_load_start_ts
  ) / 1000000 AS page_load_duration_ms,
  pls.page_load_start_ts
FROM page_load_end_events AS ple
JOIN page_load_start_events AS pls
  ON ple.id = pls.id
ORDER BY
  ple.id;

DROP VIEW IF EXISTS allocation_latency;

CREATE VIEW allocation_latency AS
WITH
  allocation_done_events AS (
    SELECT
      substr(name, instr(name, '~') + 1) AS id,
      ts AS page_load_end_ts
    FROM slice
    WHERE
      category = 'blink.user_timing' AND name GLOB 'allocation-done~*'
  ),
  allocation_start_events AS (
    SELECT
      substr(name, instr(name, '~') + 1) AS id,
      ts AS page_load_start_ts
    FROM slice
    WHERE
      category = 'blink.user_timing' AND name GLOB 'allocation-start~*'
  )
SELECT
  alloc_done.id,
  (
    alloc_done.page_load_end_ts - alloc_start.page_load_start_ts
  ) / 1000000 AS allocation_duration_ms
FROM allocation_done_events AS alloc_done
JOIN allocation_start_events AS alloc_start
  ON alloc_done.id = alloc_start.id
ORDER BY
  alloc_done.id;

DROP VIEW IF EXISTS tab_timing;

CREATE VIEW tab_timing AS
SELECT
  ol.id,
  ol.page_load_duration_ms,
  al.allocation_duration_ms,
  ol.page_load_start_ts
FROM open_latency AS ol
JOIN allocation_latency AS al
  ON ol.id = al.id
ORDER BY
  ol.id;

DROP VIEW IF EXISTS tab_timing_by_iteration;

CREATE VIEW tab_timing_by_iteration AS
SELECT
  iterations.id AS it_id,
  tab_timing.id AS tab_index,
  tab_timing.page_load_duration_ms AS page_load_duration_ms,
  tab_timing.allocation_duration_ms AS allocation_duration_ms
FROM iterations
JOIN tab_timing
  ON tab_timing.page_load_start_ts >= iterations.start
  AND tab_timing.page_load_start_ts <= iterations.end;
