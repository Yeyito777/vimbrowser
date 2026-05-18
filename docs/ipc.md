# vimbrowser IPC

This is the canonical local automation and control interface for vimbrowser.

If a script, test, external agent, or future integration needs to observe or control the application shell, prefer extending this IPC protocol instead of adding ad-hoc diagnostics, scraping logs, driving DevTools/CDP for chrome state, or special-casing one-off command paths.

## Transport

- Protocol name: `vimbrowser-ipc`
- Current protocol version: `1`
- Transport: Unix-domain stream socket (`AF_UNIX`, `SOCK_STREAM`)
- Socket path:
  - with `--profile-dir DIR`: `DIR/ipc.sock`
  - without `--profile-dir`: an instance-local runtime path next to the
    temporary per-process state file
- Client override: set `VIMBROWSER_IPC=/path/to/ipc.sock`
- Client profile shortcut: set `VIMBROWSER_PROFILE_DIR=DIR` to connect to
  `DIR/ipc.sock`. The installed user wrapper defaults to
  `/home/yeyito/.runtime/vimbrowser-yeyito`, and `scripts/vimbrowser-ipc`
  auto-detects that socket when it exists.
- Socket permissions: `0600`

The socket is local-user app IPC. It is not a remote network API and should stay unavailable to other users by default.

## Framing

Each connection carries exactly one command and one response.

1. The client connects to the socket.
2. The client writes one command line, terminated by `\n` or EOF.
3. The browser runs the command on the CEF UI thread.
4. The browser writes one response, terminated by `\n`, then closes the connection.

Current command text is simple ASCII/UTF-8-ish whitespace-tokenized text. Responses are either:

- JSON objects for structured state-changing/status commands.
- Plain text for small scalar values and errors.

Errors start with `ERR `.

## Reference client

Use `scripts/vimbrowser-ipc` as the canonical CLI wrapper:

```bash
scripts/vimbrowser-ipc status
scripts/vimbrowser-ipc version
VIMBROWSER_IPC=/tmp/test/ipc.sock scripts/vimbrowser-ipc status
```

The script is intentionally thin. Protocol semantics belong in the browser command dispatcher, not in the wrapper.

## Commands

### `version` / `protocol`

Returns protocol metadata:

```json
{"protocol":"vimbrowser-ipc","version":1}
```

Use this before depending on newer commands from external integrations.

### `status` / `json`

Returns canonical app/tab state as JSON. Current fields:

```json
{
  "ipc_protocol": "vimbrowser-ipc",
  "ipc_version": 1,
  "active_index": 0,
  "active_tab": 1,
  "tabs": 1,
  "url": "https://example.com/",
  "title": "Example Domain",
  "showfps": false,
  "shader": true,
  "fps_has_sample": true,
  "fps": 165,
  "refresh_rate": 164.99
}
```

Notes:

- `active_index` is zero-based for code-facing consumers.
- `active_tab` is one-based for user-facing scripts.
- `fps` is `null` when no native compositor sample is available.
- `refresh_rate` is the active browser compositor's current refresh rate.

### `fps`

Returns the rounded active-browser FPS sample, or `--` when no sample is available.

### `refresh`

Returns the active browser compositor refresh rate as a decimal number.

### `url`

Returns the active tab's URL as plain text.

### `showfps [on|off]`

Toggles or sets the visible FPS overlay and returns `status` JSON.

### `shader [on|off]`

Toggles or sets the native Blink page color shader and returns `status` JSON.

### `scroll <dy> [count]`

Scrolls the active page by `dy` pixels, repeated `count` times. Returns `status` JSON.

`count` is clamped by the browser for safety.

### `tab <1-based-index>`

Activates the requested tab and returns `status` JSON.

### `help`

Returns a plain text command summary.

## Compatibility rules

This protocol is allowed to grow, but it should remain script-friendly and stable.

- Keep the Unix socket path and one-command-per-connection framing stable unless there is a deliberate version bump.
- Add commands rather than changing existing command semantics.
- Add JSON fields rather than removing or renaming existing fields.
- Keep `status` as the primary machine-readable state snapshot.
- Keep `version` available for feature detection.
- Document every user-facing command here when adding it.

## Implementation map

- Server transport: `src/ipc_server.{h,cc}`
- Command dispatcher: `BrowserWindow::HandleIpcCommand` in `src/browser_window.cc`
- Reference CLI: `scripts/vimbrowser-ipc`

Future IPC work should start from those files and this document.
