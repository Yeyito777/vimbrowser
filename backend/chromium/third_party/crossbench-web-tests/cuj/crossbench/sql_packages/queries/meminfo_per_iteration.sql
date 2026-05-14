-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE sql_packages.queries.meminfo_total;

-- Compute the slope of the least-squares approximation line for meminfo samples
-- collected with the same title across multiple iterations.
-- x-axis: it_id; slope is change in memory per iteration
-- y-axis: pss_total_mb + swap_total_mb, combine pss and swap to track memory

DROP VIEW IF EXISTS per_iteration;

-- First, average multiple meminfo samples in the same iteration with the same
-- title together so that there is only one y value per iteration.
CREATE VIEW per_iteration AS
SELECT
  title,
  CAST(it_id AS REAL) AS x,
  avg(pss_total_mb + swap_total_mb) AS process_mb,
  avg(dma_buf_mb) AS dma_buf_mb,
  avg(free_mb) AS free_mb
FROM meminfo_total_output
-- Filter out setup iterations, keeping only integer it_ids.
WHERE
  it_id = CAST(CAST(it_id AS INTEGER) AS STRING)
GROUP BY
  x,
  title;

DROP VIEW IF EXISTS mean;

CREATE VIEW mean AS
SELECT
  title,
  avg(x) AS x,
  avg(process_mb) AS process_mb,
  avg(dma_buf_mb) AS dma_buf_mb,
  avg(free_mb) AS free_mb
FROM per_iteration
GROUP BY
  title;

DROP TABLE IF EXISTS meminfo_per_iteration_output;

-- Compute the least squares slope value using the formula from:
--  https://en.wikipedia.org/wiki/Simple_linear_regression
-- slope = sum((x_i - x_mean) * (y_i - y_mean)) / sum((x_i - x_mean)^2)
-- We do this by computing the mean above and joining it to each sample.
CREATE PERFETTO TABLE meminfo_per_iteration_output AS
SELECT
  per_iteration.title AS title,
  sum(
    (
      per_iteration.x - mean.x
    ) * (
      per_iteration.process_mb - mean.process_mb
    )
  ) / sum((
    per_iteration.x - mean.x
  ) * (
    per_iteration.x - mean.x
  )) AS process_mb,
  sum(
    (
      per_iteration.x - mean.x
    ) * (
      per_iteration.dma_buf_mb - mean.dma_buf_mb
    )
  ) / sum((
    per_iteration.x - mean.x
  ) * (
    per_iteration.x - mean.x
  )) AS dma_buf_mb,
  sum((
    per_iteration.x - mean.x
  ) * (
    per_iteration.free_mb - mean.free_mb
  )) / sum((
    per_iteration.x - mean.x
  ) * (
    per_iteration.x - mean.x
  )) AS free_mb
FROM per_iteration
JOIN mean
  ON per_iteration.title = mean.title
GROUP BY
  per_iteration.title;
