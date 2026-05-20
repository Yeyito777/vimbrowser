# vimbrowser IPC

This is the canonical local automation and control interface for vimbrowser.

If a script, test, external agent, or future integration needs to observe or control the application shell, prefer extending this IPC protocol instead of adding ad-hoc diagnostics, scraping logs, driving DevTools/CDP for chrome state, or special-casing one-off command paths.

## Transport

- Protocol name: `vimbrowser-ipc`
- Current protocol version: `1`
- Transport: Unix-domain stream socket (`AF_UNIX`, `SOCK_STREAM`)
- Socket path:
  - with `--profile-dir DIR`: `DIR/ipc.sock`
  - without `--profile-dir`: an instance-local runtime path next to the temporary per-process state file
- Client override: set `VIMBROWSER_IPC=/path/to/ipc.sock`
- Client profile shortcut: set `VIMBROWSER_PROFILE_DIR=DIR` to connect to `DIR/ipc.sock`. The installed user wrapper defaults to `/home/yeyito/.runtime/vimbrowser-yeyito`, and `scripts/vimbrowser-ipc` auto-detects that socket when it exists.
- Socket permissions: `0600`

The socket is local-user app IPC. It is not a remote network API and should stay unavailable to other users by default.

## Framing

Each connection carries exactly one command and one response.

1. The client connects to the socket.
2. The client writes one command line, terminated by `\n` or EOF.
3. The browser runs the command on the CEF UI thread. Commands that need native asynchronous CEF/renderer callbacks hold the connection until the callback replies or the IPC server times out.
4. The browser writes one response, terminated by `\n`, then closes the connection.

Current command text is simple whitespace-tokenized UTF-8-ish text. This keeps the protocol scriptable; commands that need spaces in a URL/search query join the remaining arguments themselves.

Responses are either:

- JSON objects for structured state-changing/status/debug commands.
- Plain text for scalar values, HTML/text/body dumps, and errors.

Errors start with `ERR `.

## Reference client

Use `scripts/vimbrowser-ipc` as the canonical CLI wrapper:

```bash
scripts/vimbrowser-ipc status
scripts/vimbrowser-ipc tabs
scripts/vimbrowser-ipc tab-focus 3
VIMBROWSER_PROFILE_DIR=/tmp/my-vimbrowser-profile scripts/vimbrowser-ipc tabs
VIMBROWSER_IPC=/tmp/test/ipc.sock scripts/vimbrowser-ipc status
```

The script is intentionally thin. Protocol semantics belong in the browser command dispatcher, not in the wrapper.

## Tab IDs versus indexes

`tabid` and tab index are deliberately different:

- `tabid` is a runtime-stable monotonically increasing integer assigned when a tab backend is created.
- `tabid` is not reused during the process lifetime.
- `tabid` does not change when tabs are reordered.
- `index` is the current zero-based position in the tab vector and can change.
- `tab` is the one-based UI-friendly position.

Use ID-based commands for automation. Keep `tab <1-based-index>` only for legacy index-style scripts.

## Commands

### Metadata

#### `version` / `protocol`

Returns protocol metadata:

```json
{"protocol":"vimbrowser-ipc","version":1}
```

#### `help`

Returns a human-readable list of available commands.

#### `commands`

Returns machine-readable command metadata as JSON:

```json
{"commands":[{"name":"tabs","usage":"tabs","description":"list all tabs with stable tab ids"}]}
```

### Status and tabs

#### `status` / `json`

Returns canonical app/tab state as JSON. Fields include:

```json
{
  "ipc_protocol": "vimbrowser-ipc",
  "ipc_version": 1,
  "active_tabid": 1,
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

#### `tabs`

Returns all tabs as JSON:

```json
{
  "ipc_protocol": "vimbrowser-ipc",
  "ipc_version": 1,
  "active_tabid": 3,
  "active_index": 1,
  "active_tab": 2,
  "tabs": [
    {
      "id": 2,
      "index": 0,
      "tab": 1,
      "active": false,
      "url": "https://example.com/",
      "title": "Example Domain",
      "loading": false,
      "can_go_back": true,
      "can_go_forward": false,
      "fps_has_sample": true,
      "fps": 165,
      "refresh_rate": 164.99
    }
  ]
}
```

Notes:

- `index` is zero-based.
- `tab` is one-based.
- `active_tabid` is the stable runtime tab ID.
- `fps` is `null` when no native compositor sample is available.

### Tab control

#### `tab-focus <tabid>`

Focuses a tab by stable ID. Returns `status` JSON.

#### `tab-delete <tabid>`

Deletes a tab by stable ID, using the same backend-destroying close path as the vim `d`/`D` bindings. Returns `status` JSON.

#### `tab-order <tabid> <zero-based-index>`

Moves a tab to a target zero-based index. The target is clamped into the valid range. The tab's ID is preserved and the active tab identity is preserved across the reorder. Returns `tabs` JSON.

#### `open-tab <url-or-query-or-local-path>`

Resolves the text with the same native URL/search/local-file path used by `:open`, records open history, opens a new active tab, and returns `status` JSON.

#### `open <tabid> <url-or-query-or-local-path>`

Resolves the text with the native URL/search/local-file path, records open history, and loads it into an existing tab. The target tab is not implicitly activated. Returns `tabs` JSON.

#### `tab <1-based-index>`

Legacy focus by current one-based index. Returns `status` JSON.

#### `tab-close [tabid]`

With no argument, closes the active tab. With a `tabid`, closes that stable-ID tab. In both forms the backend browser instance is destroyed. Returns `status` JSON.

### Navigation, zoom, and scrolling

#### `reload [tabid]`

Reloads the active tab or the specified tab. Returns `tabs` JSON.

#### `reload-ignore-cache [tabid]`

Hard-reloads the active tab or specified tab. Returns `tabs` JSON.

#### `back [tabid]` / `forward [tabid]`

Runs native back/forward navigation when possible. Returns `tabs` JSON.

#### `stop [tabid]`

Stops loading the active tab or specified tab. Returns `tabs` JSON.

#### `zoom [tabid] <in|out|reset|level>`

Runs native CEF zoom on the active tab or specified tab. `level` is CEF's numeric zoom level, not a percentage. Returns `tabs` JSON.

#### `scroll <dy> [count]`

Scrolls the active page by `dy` pixels, repeated `count` times. Returns `status` JSON. `count` is clamped by the browser for safety.

#### `scroll-tab <tabid> <dy> [count]`

Scrolls a specific tab by stable ID. Returns `tabs` JSON.

### Existing scalar/toggle commands

#### `fps`

Returns the rounded active-browser FPS sample, or `--` when no sample is available.

#### `refresh`

Returns the active browser compositor refresh rate as a decimal number.

#### `url`

Returns the active tab's URL as plain text.

#### `showfps [on|off]`

Toggles or sets the visible FPS overlay and returns `status` JSON.

#### `shader [on|off]`

Toggles or sets the native Blink/native-theme page color shader and returns `status` JSON.

### Native debugging commands

These commands are implemented in the app/backend through CEF frame, renderer-process, cookie-manager, resource-handler, response-filter, URLRequest, and DevTools protocol APIs owned by the embedded browser. They are not wrappers around external tooling.

#### `html <tabid>`

Returns current full document HTML for the tab using `CefFrame::GetSource`.

#### `text <tabid>`

Returns current document text using `CefFrame::GetText`.

#### `screenshot <tabid>`

Captures the tab viewport as PNG without changing the focused tab. Active tabs
capture from their current surface immediately; inactive tabs are captured via
the browser backend's DevTools `Page.captureScreenshot` path, which asks the tab
renderer/compositor for an image instead of focusing the tab or reading pixels
from the host display. The raw IPC response is JSON:

```json
{"tabid":1,"url":"https://example.com/","mime_type":"image/png","encoding":"base64","data":"..."}
```

The `scripts/vimbrowser-ipc` helper decodes this command by default:

```sh
scripts/vimbrowser-ipc screenshot 1 > tab.png
scripts/vimbrowser-ipc screenshot 1 -o tab.png
scripts/vimbrowser-ipc screenshot 1 --json
```

#### `js <tabid> <javascript>`

Evaluates JavaScript in the tab renderer via a browser↔renderer process-message bridge and V8 `Eval`. Returns JSON:

```json
{"ok":true,"type":"string","result":"Example Domain"}
```

Exceptions return `{"ok":false,...}`. Promise values are reported synchronously as promises; they are not awaited yet.

#### `js-file <tabid> <path>`

Reads JavaScript from a local file in the browser process and evaluates it with the same renderer bridge as `js`.

#### `cookies <tabid>`

Lists cookies visible for the tab URL using the tab's backend `CefCookieManager`, including HttpOnly cookies:

```json
{"cookies":[{"name":"sid","value":"...","domain":"example.com","path":"/","secure":true,"httponly":true,"same_site":"lax","creation":0,"last_access":0,"has_expires":false,"expires":0}]}
```

#### `cookie-delete <tabid> <name>`

Deletes a cookie by name for the tab URL. Returns `{"deleted": N}`.

#### `cookie-set <tabid> <name> <value> [domain] [path]`

Sets a cookie for the tab URL. If `domain` is omitted CEF creates a host cookie. If `path` is omitted it defaults to `/`. Returns `{"success": true|false}`.

### Network debugging commands

Network capture is per tab and backend-owned. `BrowserClient` implements CEF request/resource hooks, assigns a per-tab monotonic request ID, stores a bounded ring of recent requests, captures headers/timing/status, and captures response bodies through a native `CefResponseFilter` up to a size limit.

#### `network <tabid> list`

Returns a list of captured requests:

```json
{"requests":[{"id":2,"url":"http://127.0.0.1/data.txt","method":"GET","resource_type":"xhr","status":200,"complete":true,"body_size":15,"body_truncated":false,"duration_ms":1.2}]}
```

#### `network <tabid> detail <requestid>`

Returns full metadata for a captured request, including request/response headers and request body preview.

#### `network <tabid> body <requestid>`

Returns the captured response body bytes/text directly. Bodies are capped by the browser; check `body_truncated` in `list`/`detail`.

#### `network <tabid> replay <requestid>`

Replays a captured request using native `CefURLRequest` in the tab's request context and returns JSON with status, headers, and body. Requests with truncated request bodies are refused instead of replaying partial data.

#### `network <tabid> clear`

Clears the tab's in-memory captured network request ring. Returns `{"cleared":true}`.

## Compatibility rules

This protocol is allowed to grow, but it should remain script-friendly and stable.

- Keep the Unix socket path and one-command-per-connection framing stable unless there is a deliberate version bump.
- Add commands rather than changing existing command semantics.
- Add JSON fields rather than removing or renaming existing fields.
- Keep `status` as the primary machine-readable state snapshot.
- Keep `version` available for feature detection.
- Document every user-facing command here when adding it.

## Implementation map

- Server transport and async response waiting: `src/ipc_server.{h,cc}`
- Command dispatcher and tab/debug orchestration: `BrowserWindow::HandleIpcCommand` / `HandleIpcCommandAsync` in `src/browser_window.cc`
- Stable tab ID storage: `src/tab.h`
- Network capture/replay request construction: `src/browser_client.{h,cc}`
- Renderer-side JS bridge: `src/app.{h,cc}`
- Reference CLI: `scripts/vimbrowser-ipc`

Future IPC work should start from those files and this document.
