-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE sql_packages.web_tests_common.iterations;

DROP VIEW IF EXISTS renderer_created;

CREATE VIEW renderer_created AS
SELECT
  extract_arg(ftrace_event.arg_set_id, 'pid') AS forked_pid,
  process.cmdline AS process_cmdline,
  ts
FROM ftrace_event
JOIN process
  ON forked_pid = process.pid
WHERE
  ftrace_event.name = 'task_newtask'
  AND (
    -- Android Chrome process cmd format
    process.cmdline GLOB '*org.chromium.content.app.SandboxedProcessService*'
    -- ChromeOS Chrome process cmd format
    OR process.cmdline GLOB '/opt/google/chrome/chrome --type=renderer*'
  );

DROP VIEW IF EXISTS renderers_by_iteration;

CREATE VIEW renderers_by_iteration AS
SELECT
  iterations.id AS it_id,
  process_cmdline
FROM iterations
JOIN renderer_created
  ON renderer_created.ts >= iterations.start AND renderer_created.ts <= iterations.end;

DROP TABLE IF EXISTS total_renderers_output;

CREATE PERFETTO TABLE total_renderers_output AS
SELECT
  it_id,
  count(*) AS total_renderers
FROM renderers_by_iteration
GROUP BY
  it_id;
