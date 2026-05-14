-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
DROP VIEW IF EXISTS page_load_start_end;

CREATE VIEW page_load_start_end AS
SELECT
  *
FROM (
  SELECT
    row_number() OVER (ORDER BY ts) AS id,
    ts AS page_load_start
  FROM slice
  WHERE
    category = 'blink.user_timing' AND name = 'page-load'
)
JOIN (
  SELECT
    row_number() OVER (ORDER BY ts) AS id,
    ts AS page_load_end
  FROM slice
  WHERE
    category = 'blink.user_timing' AND name = 'page-loaded'
)
  USING (id);

DROP VIEW IF EXISTS page_load_dur;

CREATE VIEW page_load_dur AS
SELECT
  id,
  (
    SELECT
      dur / 1000000
    FROM slice AS s
    WHERE
      s.name = 'PageLoadMetrics.NavigationToLargestContentfulPaint'
      AND s.ts > plse.page_load_start
      AND s.ts < plse.page_load_end
  ) AS page_load_time
FROM page_load_start_end AS plse;

DROP VIEW IF EXISTS link_click_start_end;

CREATE VIEW link_click_start_end AS
SELECT
  *
FROM (
  SELECT
    row_number() OVER (ORDER BY ts) AS id,
    ts AS link_click_start
  FROM slice
  WHERE
    category = 'blink.user_timing' AND name = 'click-link-on-page'
)
JOIN (
  SELECT
    row_number() OVER (ORDER BY ts) AS id,
    ts AS link_click_end
  FROM slice
  WHERE
    category = 'blink.user_timing' AND name = 'click-link-on-page-loaded'
)
  USING (id);

DROP VIEW IF EXISTS link_click_time;

CREATE VIEW link_click_time AS
SELECT
  id,
  (
    SELECT
      s.ts
    FROM slice AS s
    WHERE
      s.name = 'EventLatency'
      AND extract_arg(s.arg_set_id, 'event_latency.event_type') = 'GESTURE_TAP_DOWN'
      AND s.ts > lcse.link_click_start
      AND s.ts < lcse.link_click_end
  ) AS link_click_ts
FROM link_click_start_end AS lcse;

DROP VIEW IF EXISTS link_click_loaded;

CREATE VIEW link_click_loaded AS
SELECT
  id,
  (
    SELECT
      s.ts + s.dur
    FROM slice AS s
    WHERE
      s.name = 'PageLoadMetrics.NavigationToLargestContentfulPaint'
      AND s.ts > lcse.link_click_start
      AND s.ts < lcse.link_click_end
  ) AS link_click_loaded_ts
FROM link_click_start_end AS lcse;

DROP VIEW IF EXISTS link_click_dur;

CREATE VIEW link_click_dur AS
SELECT
  id,
  (
    (
      link_click_loaded_ts - link_click_ts
    ) / 1000000
  ) AS link_load_time
FROM link_click_time
JOIN link_click_loaded
  USING (id);

CREATE PERFETTO TABLE page_click_output AS
SELECT
  *
FROM page_load_dur
JOIN link_click_dur
  USING (id);
