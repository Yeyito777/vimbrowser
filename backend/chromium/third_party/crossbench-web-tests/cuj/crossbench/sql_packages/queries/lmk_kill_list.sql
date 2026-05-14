-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE sql_packages.web_tests_common.iterations;

DROP VIEW IF EXISTS lmk_kill_occurred;

CREATE VIEW lmk_kill_occurred AS
SELECT
  ts,
  extract_arg(arg_set_id, 'lmk_kill_occurred.oom_adj_score') AS oom_adj_score,
  extract_arg(arg_set_id, 'lmk_kill_occurred.min_oom_score') AS min_oom_score,
  extract_arg(arg_set_id, 'lmk_kill_occurred.page_fault') AS page_fault,
  extract_arg(arg_set_id, 'lmk_kill_occurred.page_major_fault') AS page_major_fault,
  extract_arg(arg_set_id, 'lmk_kill_occurred.rss_in_bytes') AS rss_in_bytes,
  extract_arg(arg_set_id, 'lmk_kill_occurred.cache_in_bytes') AS cache_in_bytes,
  extract_arg(arg_set_id, 'lmk_kill_occurred.swap_in_bytes') AS swap_in_bytes,
  extract_arg(arg_set_id, 'lmk_kill_occurred.free_mem_kb') AS free_mem_kb,
  extract_arg(arg_set_id, 'lmk_kill_occurred.free_swap_kb') AS free_swap_kb,
  extract_arg(arg_set_id, 'lmk_kill_occurred.reason') AS reason,
  extract_arg(arg_set_id, 'lmk_kill_occurred.thrashing') AS thrashing,
  extract_arg(arg_set_id, 'lmk_kill_occurred.max_thrashing') AS max_thrashing,
  extract_arg(arg_set_id, 'lmk_kill_occurred.total_foreground_services') AS total_foreground_services,
  extract_arg(arg_set_id, 'lmk_kill_occurred.procs_with_foreground_services') AS procs_with_foreground_services,
  extract_arg(arg_set_id, 'lmk_kill_occurred.process_name') AS process_name
FROM slice
WHERE
  name = 'lmk_kill_occurred';

DROP VIEW IF EXISTS lmk_kill_list_output;

CREATE VIEW lmk_kill_list_output AS
SELECT
  iterations.id AS it_id,
  lmk_kill_occurred.*
FROM iterations
JOIN lmk_kill_occurred
  ON lmk_kill_occurred.ts >= iterations.start
  AND lmk_kill_occurred.ts <= iterations.end;
