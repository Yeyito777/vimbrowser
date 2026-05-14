-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.

INCLUDE PERFETTO MODULE sql_packages.web_tests_common.iterations;

DROP VIEW IF EXISTS tabs_alive;

CREATE VIEW tabs_alive AS
WITH
  page_loaded_events AS (
    -- 1. Get all page-loaded events with their process info
    SELECT
      CAST(substr(s.name, instr(s.name, '~') + 1) AS INTEGER) AS page_loaded_tab_index,
      s.ts AS page_loaded_ts,
      p.pid AS page_loaded_pid
    FROM slice AS s
    JOIN thread_track AS tt
      ON s.track_id = tt.id
    JOIN thread AS t
      USING (utid)
    JOIN process AS p
      USING (upid)
    WHERE
      s.cat = 'blink.user_timing' AND s.name GLOB 'page-loaded~*'
  ),
  process_lifetimes AS (
    -- 2. Determine the start and end timestamps for all processes
    --    Note: process.end_ts might be NULL if process is alive at trace end.
    --    We'll use trace_end as a fallback for 'alive' processes.
    SELECT
      ple.page_loaded_tab_index AS tab_index,
      p.pid,
      p.name AS process_name,
      p.start_ts,
      coalesce(p.end_ts, (
        SELECT
          max(ts)
        FROM slice
      )) AS end_ts_effective,
      ple.page_loaded_ts AS page_loaded_ts
    FROM process AS p
    JOIN page_loaded_events AS ple
      ON p.pid = ple.page_loaded_pid
    WHERE
      ple.page_loaded_ts BETWEEN p.start_ts AND coalesce(p.end_ts, (
        SELECT
          max(ts)
        FROM slice
      ))
  )
SELECT
  pl_outer.tab_index,
  pl_outer.page_loaded_ts,
  pl_outer.pid,
  pl_outer.process_name,
  (
    SELECT
      count(*)
    FROM process_lifetimes AS pl_inner
    WHERE
      -- Process must be emitted a 'page-loaded' event before the current tab's
      -- 'page-loaded' event and must still be alive
      pl_inner.page_loaded_ts <= pl_outer.page_loaded_ts
      AND pl_inner.end_ts_effective >= pl_outer.page_loaded_ts
  ) AS tabs_alive
FROM process_lifetimes AS pl_outer
ORDER BY
  pl_outer.tab_index;

DROP VIEW IF EXISTS tabs_alive_by_iteration;

CREATE VIEW tabs_alive_by_iteration AS
SELECT
  iterations.id AS it_id,
  tabs_alive.tab_index,
  tabs_alive.tabs_alive,
  tabs_alive.pid,
  tabs_alive.process_name
FROM iterations
JOIN tabs_alive
  ON tabs_alive.page_loaded_ts >= iterations.start
  AND tabs_alive.page_loaded_ts <= iterations.end;

CREATE TABLE avg_tabs_alive_after_first_kill AS
WITH
  tabs_alive_with_prev_count AS (
    SELECT
      it_id,
      tab_index,
      tabs_alive,
      lag(tabs_alive, 1, -1) OVER (PARTITION BY it_id ORDER BY tab_index) AS prev_tabs_alive
    FROM tabs_alive_by_iteration
  ),
  first_kill_point AS (
    SELECT
      it_id,
      min(tab_index) AS first_kill_tab_index
    FROM tabs_alive_with_prev_count
    WHERE
      -- condition for no increase (i.e., decrease or stay the same)
      tabs_alive <= prev_tabs_alive
    GROUP BY
      it_id
  )
SELECT
  fkp.it_id,
  fkp.first_kill_tab_index,
  avg(tai.tabs_alive) AS average_tabs_alive_after_kill
FROM first_kill_point AS fkp
JOIN tabs_alive_by_iteration AS tai
  ON fkp.it_id = tai.it_id AND tai.tab_index >= fkp.first_kill_tab_index
GROUP BY
  fkp.it_id,
  -- group by this to ensure it's in the output if needed
  fkp.first_kill_tab_index
ORDER BY
  fkp.it_id;
