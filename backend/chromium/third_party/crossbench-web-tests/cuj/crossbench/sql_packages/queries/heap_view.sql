-- Copyright 2025 The Chromium Authors
-- Use of this source code is governed by a BSD-style license that can be
-- found in the LICENSE file.

DROP VIEW IF EXISTS heap_view;

CREATE VIEW heap_view AS
WITH
RECURSIVE   view_subclasses AS (
    SELECT
      id,
      name
    FROM heap_graph_class
    WHERE
      name = 'android.view.View'
    UNION ALL
    SELECT
      hgc.id,
      hgc.name
    FROM heap_graph_class AS hgc
    JOIN view_subclasses AS vs
      ON hgc.superclass_id = vs.id
  )
SELECT
  count(DISTINCT obj.id) AS count,
  count(DISTINCT ref.owner_id) AS attached_count
FROM heap_graph_object AS obj
JOIN view_subclasses AS vs
  ON obj.type_id = vs.id
LEFT JOIN heap_graph_reference AS ref
  ON obj.id = ref.owner_id
  AND ref.field_name = 'android.view.View.mAttachInfo'
  AND ref.owned_id IS NOT NULL
WHERE
  obj.reachable = 1;
