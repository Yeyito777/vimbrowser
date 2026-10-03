# Background automation update (macOS)

Upstream `991414c77abeee80a3851ee9668a2bcc709f6470` added background
open/context commands and a CEF focus handler. The initial macOS update
backported that fix separately from the Chromium platform-removal migration.
Upstream through `e1fade81d0` is now merged, retaining these local safeguards:

- Makes IPC `open-tab` and `open-context-tab` background by default.
  `open-focus-tab`, `open-focus-context-tab`, `tab-focus`, and sidebar selection/
  focus commands remain explicit opt-ins. Desktop URL events explicitly use
  foreground opening; interactive browser keyboard commands are unchanged.
- Requires the native window to already be active before CEF can focus its
  active/visible BrowserView. Background popup creation cannot select a tab;
  late popup callbacks recheck their opener and native activation.
  JavaScript/trusted-control automation also marks its source tab's popups as
  background until that tab is explicitly activated again; synthetic trusted
  user activation is not treated as permission to select a new tab.
- Materializes the explicitly addressed lazy tab for page-directed IPC and
  waits up to ten seconds for its initial document without selecting it.
  Metadata queries do not wake lazy tabs. Named-context initialization leaves
  other dormant tabs untouched.
- Preserves sidebar folder, selection, visual anchor, and scroll during IPC
  folder creation. Moving the selected item out of view requires `--force`.
  Explicit deletion necessarily removes its target; it is not a focus command.
- Persists isolated context tabs using the independent record format from
  upstream `8dd9f2b220d9a847a8a7b149fe63872e70751384`. The full merge also
  preserves stable tab IDs, the allocator, network-broker commands and bounded
  background activity leases; page-readiness retries still do not select tabs.

The shell can still build against the cached macOS CEF distribution. The
private activation adapter follows that distribution's header signature;
newer distributions receive the explicit activation option and older ones
retain their original behavior. Keep each distribution's headers, wrapper and
framework together. Folder-targeted opening changes only shell IPC code and
does not require rebuilding Chromium or regenerating the CEF distribution.

The Exocortex CLI update through upstream `c8ce75d` uses background opening and
stdin-only JavaScript/raw payloads. Integrations must not silently fall back to
foreground opening on an old server; an unsupported background command fails.

## Regression checks

```sh
clang++ -std=c++20 -Isrc tests/context_state_test.cc src/config.cc \
  -o build-mac.noindex/context-state-test
build-mac.noindex/context-state-test
swiftc tests/foreground_monitor.swift -o build-mac.noindex/foreground-monitor
python3 tests/background_focus_e2e.py \
  --binary build-mac.noindex/Release/vimbrowser.app/Contents/MacOS/vimbrowser \
  --cli /path/to/vimbrowser-cli/bin/vimbrowser-cli \
  --foreground-monitor build-mac.noindex/foreground-monitor
```

The runtime test creates its own `/tmp` profile and local fixture pages.
`VIMBROWSER_TEST_NO_ACTIVATE=1` prevents showing its window and prohibits AppKit
activation; a read-only NSWorkspace monitor also verifies that the test
application never becomes the OS foreground app. It never connects to the user's
profile. This validates IPC selection/native rejection in a hidden window, not
a manual foreground-window UX test. Do not set that environment variable for
normal browsing.

## Safe installation while the old app is running

Stage a complete signed bundle outside the running app. Do not overwrite or
rename the running bundle: future helper launches/resource loads could otherwise
mix versions. After the **user** quits, run:

```sh
python3 scripts/install-mac-app-when-stopped.py \
  --staged-app /path/to/staged/vimbrowser.app \
  --ipc-binary build-mac.noindex/Release/vimbrowser-ipc
```

The installer refuses while any executable under the old bundle is running.
It verifies the staged arm64 signature, moves the old bundle into a timestamped
hidden rollback directory, installs the staged bundle by rename, and registers
it without launching anything. It never stops applications or changes profiles.
Before upgrading the old URL-only-state version, retain the named-context tab
metadata and reopen those tabs in their original named contexts after relaunch
(or migrate the records while stopped). Existing context storage stays on disk.
