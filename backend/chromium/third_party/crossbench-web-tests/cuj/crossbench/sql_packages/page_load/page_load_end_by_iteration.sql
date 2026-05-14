-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE sql_packages.web_tests_common.iterations;

INCLUDE PERFETTO MODULE sql_packages.page_load.page_load_end;

-- Add the iteration id to the page load end table.
DROP VIEW IF EXISTS page_load_end_by_iteration;

CREATE VIEW page_load_end_by_iteration AS
SELECT
  page_load_end.id,
  iterations.id AS it_id,
  page_load_end.page_load_end,
  page_load_end.detail
FROM iterations
JOIN page_load_end
  ON page_load_end.page_load_end >= iterations.start
  AND page_load_end.page_load_end <= iterations.end;
