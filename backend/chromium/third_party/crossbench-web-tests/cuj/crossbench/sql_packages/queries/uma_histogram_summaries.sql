-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE chrome.histograms;

INCLUDE PERFETTO MODULE sql_packages.queries.uma_histogram_constants;

DROP TABLE IF EXISTS uma_histogram_summaries;

CREATE PERFETTO TABLE uma_histogram_summaries AS
SELECT
  hist.name AS hist_name,
  enum_table.enum_name AS "enum_name",
  avg(hist.value) AS "avg",
  count(*) AS "count",
  sum(hist.value) AS "total",
  max(hist.value) AS "max",
  percentile(hist.value, 95) AS "p95",
  percentile(hist.value, 90) AS "p90",
  percentile(hist.value, 75) AS "p75",
  percentile(hist.value, 50) AS "p50"
FROM chrome_histograms AS hist
LEFT JOIN enum_table
  ON hist.name = enum_table.histogram_name AND hist.value = enum_table.enum_value
GROUP BY
  hist_name;

-- Create a macro to extract the enum name and count from an enum histogram.
CREATE PERFETTO MACRO uma_histogram_enum_macro(
    hist_name Expr
)
RETURNS TableOrSubquery AS
(
  SELECT
    enum_name AS name,
    "count"
  FROM uma_histogram_summaries
  WHERE
    hist_name = $hist_name
);

-- Create a macro to extract the summary stats in ms for a time histogram.
CREATE PERFETTO MACRO uma_histogram_times_macro(
    hist_name Expr,
    units_in_ms Expr
)
RETURNS TableOrSubquery AS
(
  SELECT
    "avg" / $units_in_ms AS "avg_ms",
    "count",
    total / $units_in_ms AS "sum_ms",
    "max" / $units_in_ms AS "max_ms",
    p95 / $units_in_ms AS "p95_ms",
    p90 / $units_in_ms AS "p90_ms",
    p75 / $units_in_ms AS "p75_ms",
    p50 / $units_in_ms AS "p50_ms"
  FROM uma_histogram_summaries
  WHERE
    hist_name = $hist_name
);

-- Create a macro to extract the summary stats for a count histogram.
CREATE PERFETTO MACRO uma_histogram_count_macro(
    hist_name Expr
)
RETURNS TableOrSubquery AS
(
  SELECT
    "avg",
    "count",
    total,
    "max",
    p95,
    p90,
    p75,
    p50
  FROM uma_histogram_summaries
  WHERE
    hist_name = $hist_name
);
