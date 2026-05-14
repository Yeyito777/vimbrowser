-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE sql_packages.page_load.page_load_start_end_by_iteration;

-- Get only the first page_load_start for each iteration
DROP VIEW IF EXISTS first_page_load_starts;

CREATE VIEW first_page_load_starts AS
SELECT
  it_id,
  page_load_start
FROM (
  SELECT
    it_id,
    page_load_start,
    row_number() OVER (PARTITION BY it_id ORDER BY page_load_start) AS rn
  FROM page_load_start_end_by_iteration
) AS ranked_rows
WHERE
  rn = 1;

-- Get only the last page_load_end for each iteration
DROP VIEW IF EXISTS last_page_load_ends;

CREATE VIEW last_page_load_ends AS
SELECT
  it_id,
  page_load_end
FROM (
  SELECT
    it_id,
    page_load_end,
    row_number() OVER (PARTITION BY it_id ORDER BY page_load_end DESC) AS rn
  FROM page_load_start_end_by_iteration
) AS ranked_rows
WHERE
  rn = 1;

-- Get the time difference of the first page load starts
-- and last page load ends for each iteration
DROP TABLE IF EXISTS total_load_duration_output;

CREATE PERFETTO TABLE total_load_duration_output AS
SELECT
  first_page_load_starts.it_id,
  (
    last_page_load_ends.page_load_end - first_page_load_starts.page_load_start
  ) / 1000000 AS iteration_load_duration
FROM first_page_load_starts
JOIN last_page_load_ends
  ON first_page_load_starts.it_id = last_page_load_ends.it_id;
