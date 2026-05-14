-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE sql_packages.page_load.page_load_start_end_by_iteration;

-- Output tab load duration for each tab.
DROP TABLE IF EXISTS tab_load_durations_output;

CREATE PERFETTO TABLE tab_load_durations_output AS
SELECT
  id,
  it_id,
  (
    page_load_end - page_load_start
  ) / 1000000 AS page_load_time,
  -- The URL is extracted from the detail json string.
  -- Example: '{"url":"https://storage.googleapis.com/chromiumos-test-assets-public/power_LoadTest/2023-08-09/amazon_product.html","index":0}'
  -- Extract by stripping the suffix from "," and then stripping the
  -- "url":" prefix.
  ltrim(substr(detail, 0, instr(detail, '","index')), '{"url":"') AS url
FROM page_load_start_end_by_iteration;
