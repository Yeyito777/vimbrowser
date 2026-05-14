-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE sql_packages.page_load.page_load_start_by_iteration;

INCLUDE PERFETTO MODULE sql_packages.page_load.page_load_end_by_iteration;

-- Group the page load start and end together.
DROP VIEW IF EXISTS page_load_start_end_by_iteration;

-- The start and end are joined using the detail and the iteration id columns because
-- 'page-load' and 'page-loaded' for a specific tab are not on the
-- same track (thread).
CREATE VIEW page_load_start_end_by_iteration AS
SELECT
  page_load_start_by_iteration.id AS id,
  page_load_start_by_iteration.it_id AS it_id,
  page_load_start_by_iteration.page_load_start,
  page_load_end_by_iteration.page_load_end,
  page_load_start_by_iteration.detail
FROM page_load_start_by_iteration
JOIN page_load_end_by_iteration
  USING (detail, it_id)
ORDER BY
  id;
