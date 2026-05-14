-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE sql_packages.page_load.page_load_start_by_iteration;

-- Calculate the difference between every page_load_start and the
-- previous page_load_start for each iteration.
-- Note that this query will output NULL for the first tab of each
-- iteration since there is no previous tab to compare against.
DROP TABLE IF EXISTS tab_open_latency_output;

CREATE PERFETTO TABLE tab_open_latency_output AS
SELECT
  it_id,
  id,
  (
    page_load_start - lag(page_load_start, 1, NULL) OVER (PARTITION BY it_id ORDER BY page_load_start)
  ) / 1000000 AS open_latency
FROM page_load_start_by_iteration
ORDER BY
  it_id,
  id;
