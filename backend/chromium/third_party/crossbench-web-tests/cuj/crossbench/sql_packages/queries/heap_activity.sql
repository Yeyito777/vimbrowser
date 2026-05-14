-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.
INCLUDE PERFETTO MODULE android.memory.heap_graph.dominator_tree;

DROP VIEW IF EXISTS heap_activity;

CREATE VIEW heap_activity AS
WITH
RECURSIVE   activity_subclasses AS (
    -- Base case: Select 'android.app.Activity'
    SELECT
      id,
      name
    FROM heap_graph_class
    WHERE
      name = 'android.app.Activity'
    UNION ALL
    -- Recursive step: Find classes that inherit from the classes found so far
    SELECT
      hgc.id,
      hgc.name
    FROM heap_graph_class AS hgc
    JOIN activity_subclasses AS asc
      ON hgc.superclass_id = asc.id
  )
SELECT
  count(*) AS count,
  count(DISTINCT ref.owner_id) AS activity_thread_activity_count,
  sum(dom.dominated_obj_count) AS retained_count,
  sum(dom.dominated_size_bytes) AS retained_bytes
FROM activity_subclasses AS asc
JOIN heap_graph_object AS obj
  ON obj.type_id = asc.id
JOIN heap_graph_dominator_tree AS dom
  ON dom.id = obj.id
LEFT JOIN heap_graph_reference AS ref
  ON obj.id = ref.owned_id
  AND ref.field_name = 'android.app.ActivityThread$ActivityClientRecord.activity'
WHERE
  obj.reachable = 1;
