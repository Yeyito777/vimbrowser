-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
DROP VIEW IF EXISTS gpu_context_cleanup_durations_output;

CREATE PERFETTO VIEW gpu_context_cleanup_durations_output AS
WITH
  task_duration_ms AS (
    SELECT
      (
        dur / 1000000
      ) AS dur_ms
    FROM slice
    WHERE
      -- ThreadControllerImpl::RunTask running function ScheduleGrContextCleanup.
      -- The function is called only in CrGpuMain.
      -- This requires the 'toplevel' Chrome trace category to be enabled.
      name = 'ThreadControllerImpl::RunTask'
      AND extract_arg(arg_set_id, 'task.posted_from.function_name') = 'ScheduleGrContextCleanup'
  )
SELECT
  -- Output some interesting aggregates, max, p90, p50.
  -- We typically care about long slices.
  max(dur_ms) AS max_dur,
  percentile(dur_ms, 90) AS p90_dur,
  percentile(dur_ms, 50) AS p50_dur,
  count(dur_ms) AS "count"
FROM task_duration_ms;
