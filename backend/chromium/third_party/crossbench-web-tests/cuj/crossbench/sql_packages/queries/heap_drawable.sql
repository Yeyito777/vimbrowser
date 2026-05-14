-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE android.memory.heap_graph.dominator_tree;

DROP VIEW IF EXISTS heap_drawable;

CREATE VIEW heap_drawable AS
WITH
RECURSIVE   drawable_subclases AS (
    -- Base case: Select 'android.graphics.drawable.Drawable'
    SELECT
      id,
      name
    FROM heap_graph_class
    WHERE
      name = 'android.graphics.drawable.Drawable'
    UNION ALL
    -- Recursive step: Find classes that inherit from the classes found so far
    SELECT
      hgc.id,
      hgc.name
    FROM heap_graph_class AS hgc
    JOIN drawable_subclases AS dsc
      ON hgc.superclass_id = dsc.id
  )
SELECT
  count(*) AS count,
  sum(dom.dominated_size_bytes + dom.dominated_native_size_bytes) AS retained_bytes
FROM drawable_subclases AS dsc
JOIN heap_graph_object AS obj
  ON obj.type_id = dsc.id
JOIN heap_graph_dominator_tree AS dom
  ON dom.id = obj.id
WHERE
  obj.reachable = 1;
