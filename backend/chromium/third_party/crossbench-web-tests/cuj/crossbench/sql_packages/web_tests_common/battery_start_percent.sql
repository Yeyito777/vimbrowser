-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
DROP TABLE IF EXISTS battery_start_percent;

CREATE PERFETTO TABLE battery_start_percent AS
SELECT
  c.value AS battery_start_percent
FROM counter AS c
JOIN counter_track AS t
  ON c.track_id = t.id
-- Android has the format 'batt.capacity_pct' and CrOS has the format 'batt.BAT0.capacity_pct'
-- Note: Some devices may have different battery naming (such as some mediatek platforms) and
-- therefore will not produce this metric.
WHERE
  t.name IN ('batt.BAT0.capacity_pct', 'batt.capacity_pct')
ORDER BY
  c.ts ASC
LIMIT 1;
