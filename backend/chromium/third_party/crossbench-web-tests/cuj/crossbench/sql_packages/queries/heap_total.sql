-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.

DROP VIEW IF EXISTS heap_total;

CREATE VIEW heap_total AS
SELECT
  count(*) AS count,
  sum(self_size) AS bytes,
  sum(reachable) AS reachable_count,
  sum(reachable * self_size) AS reachable_bytes
FROM heap_graph_object;
