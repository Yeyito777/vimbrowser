-- Copyright 2026 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.

DROP TABLE IF EXISTS browser_milestones;

DROP TABLE IF EXISTS zygote_forks;

DROP TABLE IF EXISTS renderer_milestones;

DROP TABLE IF EXISTS javaless_renderer_startup_output;

DROP TABLE IF EXISTS newtask_events;

DROP TABLE IF EXISTS thread_scheduled_time;

-- Extracts ftrace 'task_newtask' events to identify when new processes/threads are created.
CREATE PERFETTO TABLE newtask_events AS
SELECT
  *
FROM ftrace_event
WHERE
  name = 'task_newtask';

-- Identifies the point where the Browser process initiates a new renderer process.
CREATE PERFETTO TABLE browser_milestones AS
SELECT
  p.upid,
  p.pid AS renderer_pid,
  slice_out.ts AS rph_init_ts
FROM flow
JOIN slice AS slice_out
  ON flow.slice_out = slice_out.id
JOIN slice AS slice_in
  ON flow.slice_in = slice_in.id
JOIN thread_track AS track_in
  ON slice_in.track_id = track_in.id
JOIN thread AS thread_in
  ON track_in.utid = thread_in.utid
JOIN process AS p
  ON thread_in.upid = p.upid
WHERE
  slice_out.name = 'RenderProcessHostImpl::Init'
  AND thread_in.name = 'CrRendererMain'
  AND slice_in.name = 'RenderThreadImpl::InitializeRenderer';

-- Pre-calculates the earliest scheduled time for every thread to be used as a proxy for process start.
CREATE PERFETTO TABLE thread_scheduled_time AS
SELECT
  utid,
  min(ts) AS min_ts
FROM sched_slice
GROUP BY
  utid;

-- Tracks forks from the Android Zygote process to the Chrome renderer.
CREATE PERFETTO TABLE zygote_forks AS
SELECT
  newtask_events.ts AS zygote_fork_event_ts,
  thread_scheduled_time.min_ts AS zygote_child_scheduled_ts,
  child_thread.tid AS child_pid
FROM newtask_events
JOIN thread AS child_thread
  ON child_thread.tid = CAST(extract_arg(newtask_events.arg_set_id, 'pid') AS INTEGER)
JOIN thread_scheduled_time
  ON thread_scheduled_time.utid = child_thread.utid
JOIN thread AS t
  ON newtask_events.utid = t.utid
JOIN process AS p
  ON t.upid = p.upid
WHERE
  newtask_events.name = 'task_newtask'
  AND p.name LIKE '%_zygote_native'
  AND child_thread.tid IN (
    SELECT
      renderer_pid
    FROM browser_milestones
  );

-- Captures milestones within the sandboxed renderer process itself.
CREATE PERFETTO TABLE renderer_milestones AS
SELECT
  pr.pid AS renderer_pid,
  min(thread_scheduled_time.min_ts) AS sandboxed_process_scheduled_ts,
  min(CASE WHEN s.name = 'RenderThreadImpl::InitializeRenderer' THEN s.ts END) AS renderer_init_ts
FROM process AS pr
JOIN thread AS t
  ON t.upid = pr.upid
JOIN newtask_events
  ON newtask_events.utid = t.utid
JOIN thread AS child_thread
  ON child_thread.tid = CAST(extract_arg(newtask_events.arg_set_id, 'pid') AS INTEGER)
JOIN thread_scheduled_time
  ON thread_scheduled_time.utid = child_thread.utid
LEFT JOIN thread AS main_thread
  ON main_thread.upid = pr.upid AND main_thread.name = 'CrRendererMain'
LEFT JOIN thread_track AS tt
  ON tt.utid = main_thread.utid
LEFT JOIN slice AS s
  ON s.track_id = tt.id AND s.name = 'RenderThreadImpl::InitializeRenderer'
GROUP BY
  pr.pid;

-- Final output table combining milestones into durations for the open-close CUJ.
CREATE PERFETTO TABLE javaless_renderer_startup_output AS
SELECT
  bm.renderer_pid,
  bm.rph_init_ts AS render_process_host_impl_init_ts,
  zf.zygote_fork_event_ts,
  zf.zygote_child_scheduled_ts,
  rm.sandboxed_process_scheduled_ts,
  rm.renderer_init_ts AS render_thread_impl_initialize_renderer_ts,
  (
    rm.renderer_init_ts - bm.rph_init_ts
  ) / 1e6 AS total_startup_ms,
  (
    zf.zygote_fork_event_ts - bm.rph_init_ts
  ) / 1e6 AS chrome_browser_ms,
  (
    zf.zygote_child_scheduled_ts - zf.zygote_fork_event_ts
  ) / 1e6 AS zygote_fork_ms,
  (
    rm.sandboxed_process_scheduled_ts - zf.zygote_child_scheduled_ts
  ) / 1e6 AS android_init_ms,
  (
    rm.renderer_init_ts - rm.sandboxed_process_scheduled_ts
  ) / 1e6 AS chrome_init_ms
FROM browser_milestones AS bm
JOIN zygote_forks AS zf
  ON bm.renderer_pid = zf.child_pid
JOIN renderer_milestones AS rm
  ON bm.renderer_pid = rm.renderer_pid
ORDER BY
  bm.rph_init_ts ASC;
