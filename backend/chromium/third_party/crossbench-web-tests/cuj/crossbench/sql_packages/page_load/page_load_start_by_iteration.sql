-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE sql_packages.web_tests_common.iterations;

INCLUDE PERFETTO MODULE sql_packages.page_load.page_load_start;

-- Add the iteration id to the page load start table.
DROP VIEW IF EXISTS page_load_start_by_iteration;

CREATE VIEW page_load_start_by_iteration AS
SELECT
  page_load_start.id,
  iterations.id AS it_id,
  page_load_start.page_load_start,
  page_load_start.detail
FROM iterations
JOIN page_load_start
  ON page_load_start.page_load_start >= iterations.start
  AND page_load_start.page_load_start <= iterations.end;
