# Metrics in web-tests

This document explains how to add new metrics to `web-tests`, using the `docs`
CUJ (`cuj/crossbench/cujs/docs`) as a running example. Metrics are derived from
Perfetto traces and are defined using SQL queries and textproto files.

For a general guide to authoring tests, see
[authoring_tests.md](./authoring_tests.md). For an introduction to the HJSON
configuration format, see [hjson_config_primer.md](./hjson_config_primer.md).

# Overview

Authoring a metric in `web-tests` has the following steps:

1.  **Data Insertion**: Data points for metrics are inserted into the trace,
    either from Perfetto's built-in tracers or manually from JavaScript using
    the `performance.mark()` API.
1.  **SQL Query**: After the test, a SQL query is executed against the Perfetto
    trace to extract and transform the data into a metric.
1.  **Metric Definition**: A `.textproto` file defines the metric's ID and
    connects it to the corresponding SQL query.
1.  **Enabling the Metric**: The metric is enabled for a specific test by adding
    its `.textproto` file to the `probe-config.hjson`.
1.  **Viewing Results**: After a test run, the generated metrics are stored in
    the results directory in the `v2_metrics.textproto` summary file.

## Adding Data to Traces

### Manual Instrumentation with `performance.mark()`

For custom metrics that are specific to your test's logic, you can manually
insert events into the trace from your JavaScript code. This is done using the
User Timing API (`performance.mark()` and `performance.measure()`).

When you create a "measure", it appears as a "slice" in the Perfetto trace,
which you can then query using SQL.

**Example: The `comment-opened` metric in the `docs` CUJ**

The `docs` CUJ measures the time it takes for a comment dialog to appear after
clicking the comment button. This is implemented in
`cuj/crossbench/common-action-blocks/workspace/scripts/make-comment-marks.js`.

```javascript
// ...
performance.mark('comment-click');
// ...
// A MutationObserver waits for the comment dialog to appear, then:
performance.mark('comment-opened-next-frame');
performance.measure(
  'comment-opened',
  'comment-click',
  'comment-opened-next-frame',
);
// ...
```

This creates a slice named `comment-opened` in the trace, which is then used by
the `cuj.common-metrics.comment-opened` metric.

### Automatic Tracing

Many useful events are automatically captured by Perfetto. For example, the
`cpu_usage_stat` metric uses the `counter` and `cpu_counter_track` tables, which
are automatically populated by Perfetto to track CPU usage. The SQL query in
`cuj/crossbench/sql_packages/queries/cpu_usage_stat.sql` directly queries these
tables to calculate CPU time statistics.

The configuration for what data is collected is located in
`cuj/crossbench/trace-config/detailed-trace-config.pbtxt`. By enabling data
sources like `"linux.sys_stats"`, we ensure that these events are available for
querying.

## Creating a New Metric

Creating a new metric involves three steps: writing a SQL query, creating a
`.textproto` definition file, and enabling the metric in your test.

### Step 1: Write the SQL Query

Perfetto traces are backed by a SQLite database, so you can write standard SQL
queries to extract data. The most common table to query is the `slice` table,
which contains all the timed events from the trace.

Your SQL file should create a new table that contains the metric value.

**Example (`comment_opened.sql`):**

```sql
-- cuj/crossbench/sql_packages/queries/comment_opened.sql
DROP TABLE IF EXISTS comment_opened_output;

CREATE PERFETTO TABLE comment_opened_output AS
SELECT
  (
    SELECT
      (
        dur / 1000000
      )
    FROM slice
    WHERE
      slice.name = 'comment-opened'
  ) AS "duration_ms";
```

Place your new SQL files in `cuj/crossbench/sql_packages/queries/`.

### Step 2: Write the Metric Definition (`.textproto`)

The `.textproto` file links your metric ID to your SQL query. It can also be
used to define dimensions for your metric, which allow you to slice the data by
different categories.

**Dimensions**

Dimensions are additional columns in your output table that provide context for
your metric. For example, the `lmk_kill_list` metric has dimensions for the
iteration ID, the process name, and the timestamp of each Low Memory Killer
event. This allows you to analyze LMK events on a per-iteration and per-process
basis.

**Example (`lmk_kill_list.textproto`):**

This file defines a single query that selects a number of columns, and then
defines multiple `metric_spec` blocks that use that query's results. Each
`metric_spec` has a unique `id`, a `value` (one of the columns from the query),
and a set of `dimensions`.

```protobuf
# cuj/crossbench/common-metrics/v2/lmk_kill_list.textproto

# 1. Define the query
query: {
  id: "lmk_kill_list_query"
  table {
    table_name: "lmk_kill_list_output"
    module_name: "sql_packages.queries.lmk_kill_list"
    # 2. List all columns to be extracted
    column_names: "it_id"
    column_names: "process_name"
    column_names: "ts"
    column_names: "oom_adj_score"
    # ... other columns
  }
}

# 3. Define a metric using the query
metric_spec: {
  id: "cuj.common-metrics.lmk_kill_list.oom_adj_score"
  query {
    inner_query_id: "lmk_kill_list_query"
  }
  # 4. Define the dimensions
  dimensions: "it_id"
  dimensions: "process_name"
  dimensions: "ts"
  # 5. Define the value
  value: "oom_adj_score"
}

# ... other metric_spec blocks for other values
```

Place your new metric definition files in `cuj/crossbench/common-metrics/v2/`.

### Step 3: Enable the Metric in `probe-config.hjson`

Finally, enable your new metric for a test by adding the path to your
`.textproto` file to the `METRIC_DEFINITIONS` list in the test's
`probe-config.hjson`. The `docs` CUJ does this in
`cuj/crossbench/cujs/docs/probe-config.hjson`.

```hjson
// cuj/crossbench/cujs/docs/probe-config.hjson
{
  // ...
  args: {
    // ...
    METRIC_DEFINITIONS: [
      ../../common-metrics/v2/comment_opened.textproto
      ../../common-metrics/v2/scroll_distance.textproto
      ../../common-metrics/v2/scroll_duration.textproto
    ]
    // ...
  }
}
```

## Viewing Metric Results

After a test run, the results are stored in a timestamped directory under
`cuj/crossbench/runner/results/`. The `latest` directory is a symlink to the
most recent run.

Inside the results directory, metrics are exported to:
`TEST_NAME/pass/TIMESTAMP/first_run/trace_processor/v2_metrics.textproto`.

This file contains a summary of all the metrics from the run, bundled together.
It's a more comprehensive view and is the primary source of truth for the metric
results.

**Example `v2_metrics.textproto` output:**

```protobuf
metric_bundles {
  row {
    values {
      double_value: 420.0
    }
  }
  specs {
    id: "cuj.common-metrics.comment-opened"
    value: "duration_ms"
    query {
      table {
        table_name: "comment_opened_output"
        module_name: "sql_packages.queries.comment_opened"
        column_names: "duration_ms"
      }
    }
  }
}
```
