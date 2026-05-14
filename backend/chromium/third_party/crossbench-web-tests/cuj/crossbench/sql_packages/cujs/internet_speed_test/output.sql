-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
DROP TABLE IF EXISTS internet_speed_test_output;

CREATE PERFETTO TABLE internet_speed_test_output AS
SELECT
  CAST(extract_arg(arg_set_id, 'debug.data.detail') -> '$.download' AS REAL) AS download,
  CAST(extract_arg(arg_set_id, 'debug.data.detail') -> '$.upload' AS REAL) AS upload,
  CAST(extract_arg(arg_set_id, 'debug.data.detail') -> '$.latency' AS REAL) AS latency
FROM slice
WHERE
  category = 'blink.user_timing' AND name = 'speed-test-result';
