---
breadcrumbs:
- - /for-testers
  - For Testers
page_name: enable-logging
title: How to enable logging
---

When troubleshooting issues, a debug log can provide valuable information. This
guide explains how to enable logging in Chrome and Chromium to generate a log
file that you can attach to a bug report.

## How to Collect a Debug Log File

To create a log file, you need to launch the browser with a specific
command-line flag. This will create a file named `chrome_debug.log` on your
computer.

### On Windows

1.  Close any running instances of the browser.
2.  Launch the Command Prompt and run the following command.

```bash
path\to\chrome.exe --enable-logging --v=1
```

### On Mac and Linux

1.  Close any running instances of the browser.
2.  Launch the Terminal and run the following command:

```bash
/path/to/chrome --enable-logging --v=1
```

On these platforms, this command will create the log file in your user data
directory and will not print output to the terminal.

## Finding the Log File (`chrome_debug.log`)

After running the browser with logging enabled, the `chrome_debug.log` file will
be created. It is overwritten each time you restart the browser. Here’s where to
find it:

*   **Release builds**: In the
    [user data directory](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/user_data_dir.md),
    which is the parent directory of the `Default/` profile folder.
*   **Debug builds**: In the build output directory (e.g., `out/Debug`).
*   **Chrome OS**:
    *   Logs are available in `/var/log/chrome/`.
    *   At the login screen, they can be found in `/var/log/ui/`.
    *   For more details, see the
        [Chrome OS debugging guide](https://chromium.googlesource.com/chromiumos/docs/+/main/debugging.md).

## Controlling Verbosity

You can control the level of detail in the logs with these flags:

*   `--v=N`: Sets the default maximum active V-logging level; 0 is default. Any
    `VLOG(x)` with `x <= N` will be displayed. `1` is a good level for bug
    reports.
*   `--vmodule=module1=N,module2=M,...`: Overrides the V-logging level for
    specific modules. For example, `--vmodule=metrics=2` enables verbose logging
    for code in files named `metrics.*`. Wildcards are supported, e.g.,
    `--vmodule=*/metrics/*=2`. See details in
    [`base/logging.h`](https://chromium.googlesource.com/chromium/src/+/HEAD/base/logging.h).
    You can add `--v=-1` to suppress all other logging.

## Advanced: Viewing Logs in Real-Time (`stderr`)

For developers who want to see log output directly in the terminal, you can
direct the logs to standard error (`stderr`). This is useful for live debugging
but is not ideal for creating a log file for a bug report.

### On Linux and Mac

```bash
/path/to/chrome --enable-logging=stderr --v=1
```

This will print log messages to your terminal. You can also redirect this output
to a file:

```bash
/path/to/chrome --enable-logging=stderr --v=1 > log.txt 2>&1
```

### On Windows

On Windows, logging to `stderr` is possible, but output from sandboxed renderer
processes will not be visible in the console. The output is available to a
debugger via `OutputDebugStringA`.

## Advanced: Other Ways to Control Logging

### Overriding the Log File Path with an Environment Variable

You can force the browser to write its log file to a specific location by
setting the `CHROME_LOG_FILE` environment variable.

*   Example: `export CHROME_LOG_FILE="chrome_debug.log"` will write the log to
    the current working directory.

### Overriding the Log File Path in Tests

To override the log file path in a C++ test, use this pattern:

```cpp
#include "chrome/common/env_vars.h"
...
// Set the log file path in the environment for the test browser.
base::FilePath log_file_path = ...;
SetEnvironmentVariable(env_vars::kLogFileName, log_file_path.value().c_str());
```

### How do I specify the command line flags?

See the
[command line flags](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/command_line_flags.md)
page.

### What personal information does the log file contain?

Before attaching `chrome_debug.log` to a bug report, be aware that it can
contain some personal information, such as URLs opened during that session of
Chrome.

Since the debug log is a human-readable text file, you can open it with a text
editor and review the information it contains, and erase anything you do not
want bug investigators to see.

The boilerplate values enclosed by brackets on each line are in the format:
`[process_id:thread_id:timestamp:log_level:file_name(line_number)]`

## Legacy Tools: Sawbuck (Windows)

Sawbuck is a tool for viewing log messages on Windows in a GUI. However, it is
**no longer actively maintained** and may not work correctly with modern
versions of Chrome, especially 64-bit builds (see
[crbug.com/456884](https://crbug.com/456884)).

If you still wish to use it, you can download it from the
[Sawbuck GitHub releases page](https://github.com/google/sawbuck/releases/latest).
