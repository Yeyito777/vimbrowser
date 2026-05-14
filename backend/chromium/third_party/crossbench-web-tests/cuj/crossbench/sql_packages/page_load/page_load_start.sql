-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
-- Get the ts for the start of each page load.
-- row_number() will be the tab open index.
DROP VIEW IF EXISTS page_load_start;

CREATE VIEW page_load_start AS
SELECT
  *
FROM (
  SELECT
    row_number() OVER (ORDER BY ts) AS id,
    ts AS page_load_start,
    extract_arg(arg_set_id, 'debug.data.detail') AS detail
  FROM slice
  WHERE
    category = 'blink.user_timing' AND name = 'page-load'
)
ORDER BY
  id;
