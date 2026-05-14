-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE chrome.histograms;

DROP VIEW IF EXISTS tab_switch_latency_output;

CREATE VIEW tab_switch_latency_output AS
WITH
  tab_switch_latency AS MATERIALIZED (
    SELECT
      value
    FROM chrome_histograms
    WHERE
      name GLOB '*SwitchDuration3'
      AND ts > (
        SELECT
          min(ts)
        FROM slice
        WHERE
          cat = 'blink.user_timing' AND slice.name GLOB 'navigate_to_tab*'
      )
  )
SELECT
  avg(value) AS avg_dur,
  max(value) AS max_dur,
  percentile(value, 90) AS p90_dur,
  percentile(value, 50) AS p50_dur,
  -- Note that there *should* be 18 values per run, but we output the count here
  -- because there are cases where we get fewer values. Left as a follow-up
  -- to investigate why.
  count(value) AS "count"
FROM tab_switch_latency;
