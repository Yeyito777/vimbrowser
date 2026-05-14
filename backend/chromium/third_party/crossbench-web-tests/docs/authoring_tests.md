# Authoring Tests in web-tests

This document provides a comprehensive guide to authoring new benchmarks and
Critical User Journeys (CUJs) in `web-tests`.

For a primer on the HJSON configuration format used in `web-tests`, please read
the [HJSON Config Primer](./hjson_config_primer.md).

# Overview: CUJ vs. Benchmark

The first step in adding a new test is to determine whether it should be a CUJ
or a benchmark.

|                    | CUJ (Critical User Journey)                                         | Benchmark                                                        |
| ------------------ | ------------------------------------------------------------------- | ---------------------------------------------------------------- |
| **Implementation** | HJSON configuration on top of the `loading` test framework.         | Python, implemented directly in crossbench.                      |
| **Use Case**       | Simulating user interactions like scrolling, clicking, typing.      | Flexible workloads, long-running tests, system state monitoring. |
| **Complexity**     | Lower implementation cost, static and deterministic.                | Higher implementation cost, requires crossbench owner approval.  |
| **Control Flow**   | No runtime control flow (no loops, if/else).                        | Full Python capabilities (loops, conditionals, etc.).            |
| **Example**        | Navigating through a series of web pages and interacting with them. | Playing a video on loop until the device battery is low.         |

If you are unsure which category your test falls into, please consult with one
of the `web-tests` owners.

# Directory Structure

**Benchmarks**: `cuj/crossbench/benchmarks/<benchmark_name>/`

**CUJs**: `cuj/crossbench/cujs/<cuj_name>/`

# Common Configuration Files

Both benchmarks and CUJs use the following configuration files.

## `browser-flags.hjson` (Required)

This file specifies the command-line flags passed to the browser. Most tests
should derive from the common flags to ensure consistent behavior.

#### Example: `cuj/crossbench/benchmarks/MyTest/browser-flags.hjson`

```hjson
{
  template: ../../browser-flags/common-flags.hjson
  args: {
    // Add test-specific flags here
    CHROME_FLAGS: --disable-popup-blocking
  }
}
```

## `probe-config.hjson` (Optional)

This file defines which crossbench probes to use for collecting data. For more
details on available probes, refer to the crossbench probe documentation in
`third_party/crossbench/config/doc/probe/`.

Most tests derive from the common probe config and add test-specific probes or
metric definitions.

### Perfetto Traces (`TRACE_CONFIG`)

Perfetto traces are the primary source of metrics. All tests should enable
tracing. There are two main trace configs:

- **`detailed-trace-config.pbtxt`**: Provides highly detailed traces, ideal for
  most tests.
- **`long-running-trace-config.pbtxt`**: Use for tests that run longer than a
  few minutes to manage trace size.

#### **Example**: `cuj/crossbench/benchmarks/MyTest/probe-config.hjson`

```hjson
{
  template: ../../probe-config.hjson
  args: {
    TRACE_PROCESSOR_QUERIES: []
    METRIC_DEFINITIONS: []
    TRACE_CONFIG: ../../trace-config/detailed-trace-config.pbtxt
    ADDITIONAL_PROBES: []
  }
}
```

`TRACE_PROCESSOR_QUERIES` and `METRIC_DEFINITIONS` are used to define and
extract metrics which is discussed in [metrics.md](./metrics.md).

Perfetto traces are also useful for diagnosing performance issues. For
long-running or complex tests, you can annotate the trace to mark specific
sections of your test workload. This makes it easier to correlate trace events
with test phases.

To add annotations, execute `performance.mark("<message>")` in your test's
JavaScript at key points. These marks will appear in the Perfetto trace,
providing clear markers for your analysis.

#### **Example**:

```javascript
// Start of page navigation
performance.mark('navigate_to_page');

// After page navigation
performance.mark('navigate_to_page_end');
```

### `cb-args` (Optional)

A text file containing additional command-line flags to pass to crossbench. The
string `$[WEB_TESTS]` is replaced with the absolute path to the `web-tests`
directory.

#### **Example**: `cuj/crossbench/benchmarks/MyTest/cb-args`

```
--playback=3 --local-file-server=$[WEB_TESTS]/cuj/crossbench/MyTest/www/
```

# CUJ-Specific Configuration

CUJs require one additional file to define the user interaction workload.

## `page-config.hjson`

This file defines the sequence of actions for the CUJ.

**Schema**:

```hjson
{
  pages: {
    <page_name>: {
      // Optional: for pages requiring Google account login
      login: google
      blocks: {
        // 'setup' runs once and is not repeated with --playback
        setup: [
          { action: get, url: "https://google.com" }
          ...
        ]
        // Other blocks define the main workload
        main_actions: [
          { action: click, selector: "..." }
          ...
        ]
        ...
      }
      ...
    }
    ...
  }
}
```

### Common Actions

Here are some of the most common actions available in CUJs:

| Action                 | Description                                      |
| ---------------------- | ------------------------------------------------ |
| `get`                  | Navigates to a URL.                              |
| `click`                | Clicks on an element.                            |
| `text_input`           | Enters text into an element.                     |
| `scroll`               | Scrolls the page or an element.                  |
| `wait`                 | Pauses execution for a fixed duration.           |
| `wait_for_element`     | Waits for an element to appear in the DOM.       |
| `wait_for_ready_state` | Waits for the document's `readyState` to change. |
| `switch_tab`           | Switches to a different tab.                     |
| `close_tab`            | Closes the current tab.                          |

For a complete list of actions and their arguments, see the implementations in
`third_party/crossbench/crossbench/action_runner/action/`.

#### Playback Control

To repeat a CUJ, use the `playback` flag. It can be set in `cb-args`
(`--playback=5x` for 5 iterations) or in `page-config.hjson`:

```
{
  pages: {
    page_with_playback: {
      playback: 2h // 2 hours of playback
      actions: [
        ...
      ]
    }
  }
}
```

When using `playback`, the `setup` block is executed only once, while the other
blocks are repeated. This is useful for one-time setup followed by a repetitive
action, like stress testing.

## Variants

Variants allow you to run the same test with different configurations. To create
a variant, create a new config file with the variant name as a prefix.

- For **benchmarks**, variants are driven by `probe-config.hjson`. Create
  `<variant_name>.probe-config.hjson`.
- For **CUJs**, variants are driven by `page-config.hjson`. Create
  `<variant_name>.page-config.hjson`.

You can also create variant-specific `browser-flags.hjson` and `cb-args`, but a
matching `probe-config.hjson` (for benchmarks) or `page-config.hjson` (for CUJs)
is always required, even if it's identical to the base config.

**Example CUJ with a '1hour' and '2hour' variant**:

- `cuj/crossbench/cujs/MyCUJ/page-config.hjson` (base config, 1 hour)
- `cuj/crossbench/cujs/MyCUJ/2hour.page-config.hjson` (variant config, 2 hours)
