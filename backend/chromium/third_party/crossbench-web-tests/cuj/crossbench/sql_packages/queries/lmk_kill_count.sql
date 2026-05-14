-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE sql_packages.web_tests_common.iterations;

DROP VIEW IF EXISTS lmk_kill_ts;

CREATE VIEW lmk_kill_ts AS
SELECT
  ts
FROM slice
WHERE
  name = 'lmk_kill_occurred';

DROP TABLE IF EXISTS lmk_kill_count_output;

CREATE PERFETTO TABLE lmk_kill_count_output AS
SELECT
  iterations.id AS it_id,
  count(lmk_kill_ts.ts) AS kill_count
FROM iterations
LEFT JOIN lmk_kill_ts
  ON lmk_kill_ts.ts >= iterations.start AND lmk_kill_ts.ts <= iterations.end;
