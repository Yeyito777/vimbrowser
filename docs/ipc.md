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
- Client and browser profile shortcut: set `VIMBROWSER_PROFILE_DIR=DIR` to use
  `DIR` and connect to `DIR/ipc.sock`. An explicit browser `--profile-dir`
  takes precedence. Installed launchers and the macOS app default to their
  configured durable profile, and `scripts/vimbrowser-ipc` auto-detects that
  socket when it exists.
- Socket permissions: `0600`

The socket is local-user app IPC. It is not a remote network API and should stay unavailable to other users by default.

## Framing

Each connection carries exactly one command and one response.

1. The client connects to the socket.
2. The client writes one command line, terminated by `\n` or EOF.
3. The browser runs the command on the CEF UI thread. Commands that need native asynchronous CEF/renderer callbacks hold the connection until the callback replies or the IPC server times out.
4. The browser writes one response, terminated by `\n`, then closes the connection.

Current command text is simple whitespace-tokenized UTF-8-ish text. This keeps the protocol scriptable; commands that need spaces in a URL/search query join the remaining arguments themselves.

Opaque JavaScript is transported by automation clients through `js-base64` and
`frame-js-base64`. Their final argument is standard base64 over the exact UTF-8
source, avoiding command-line whitespace normalization and newline framing.

Responses are either:

- JSON objects for structured state-changing/status/debug commands.
- Plain text for scalar values, HTML/text/body dumps, and errors.

Errors start with `ERR `.

## Reference client

Use the installed `vimbrowser-ipc` command or the source-tree `scripts/vimbrowser-ipc` fallback as the canonical CLI wrapper:

```bash
scripts/vimbrowser-ipc status
scripts/vimbrowser-ipc tabs
scripts/vimbrowser-ipc sidebar
scripts/vimbrowser-ipc tab-focus 3
scripts/vimbrowser-ipc sidebar-select tab 3
VIMBROWSER_PROFILE_DIR=/tmp/my-vimbrowser-profile scripts/vimbrowser-ipc tabs
VIMBROWSER_IPC=/tmp/test/ipc.sock scripts/vimbrowser-ipc status
```

The client is intentionally thin. Protocol semantics belong in the browser command dispatcher, not in the wrapper. The built native client is preferred for low-latency automation; the source Perl script remains a portable fallback/reference path.

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
  "context": null,
  "audible": false,
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
      "audible": false,
      "context": null,
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
- `audible` mirrors Chromium's current per-tab audible state and drives the
  sidebar audio indicator.
- `context` is `null` for the default profile or the named isolated request
  context for that tab. The same field in `status` describes the active tab.
- `folder_id` is the tab's durable sidebar folder (`0` is the root).
- `sidebar_sort_order` is its durable order among sidebar siblings, and `pinned`
  reports whether it belongs to that folder's leading pinned section.
- `current_folder_id`, `sidebar_selected_type`, and `sidebar_selected_id`
  expose the folder view and keyboard selection independently of the active tab.
- `fps` is `null` when no native compositor sample is available.

### Sidebar folders

Folders are durable, nested, and use stable profile-persistent numeric IDs.
Folder ID `0` denotes the sidebar root and is never returned as a folder object.

#### `folders`

Returns the current folder view and every folder with `id`, `parent_id`,
`sort_order`, `pinned`, `name`, and its slash-separated `path`.

#### `folder-create <parent-folderid|0> <name>`

Creates a folder under the supplied parent. Names may contain spaces but not
`/`; sibling names are case-insensitively unique.

#### `folder-rename <folderid> <name>`

Renames a folder while preserving its ID, children, placement, and tab backends.

#### `folder-delete <folderid> <recursive|unwrap>`

`recursive` destroys every tab backend in the complete descendant tree and then
removes the folders. `unwrap` removes only the folder shell and moves its direct
tabs/subfolders to the parent in their existing order.

#### `folder-move <folderid> <parent-folderid|0>`

Moves a complete folder subtree under another folder or the root. Cycles are
rejected, and contained tab backends are neither reloaded nor recreated.

#### `folder-pin <folderid> [on|off]`

Pins or unpins a folder among its siblings. Omitting `on|off` toggles the current
state. Pinned folders and tabs share one ordered `Pinned` section, and the state
survives profile restarts.

#### `tab-folder <tabid> <folderid|0>`

Moves a tab to a folder without changing its stable runtime tab ID or reloading
its browser backend.

#### `sidebar`

Returns canonical sidebar UI state, including visibility, focus, current folder,
selected item, active search/filter state, and every currently displayed row.
Folder and tab entry rows include stable item IDs and pinned state; tab rows
also include their zero-based tab index.

#### `sidebar-folder <folderid|0>`

Browses a folder in the sidebar without activating or reloading a tab. Any active
sidebar search filter is cleared so the requested folder is shown directly.

#### `sidebar-visibility <on|off|toggle>`

Shows, hides, or toggles the sidebar. Hiding a focused sidebar returns focus to
the web view. Showing it does not implicitly steal web focus.

#### `sidebar-focus [sidebar|web]`

Moves keyboard focus to the sidebar or web view. With no argument it focuses the
sidebar and makes it visible. An active command/search prompt is canceled first.

#### `sidebar-select <tab|folder|parent> [id]`

Moves only the sidebar cursor, without activating a tab or entering a folder.
Selecting a tab or folder automatically browses its containing folder, so nested
items can be selected directly by stable ID. Forms are:

```text
sidebar-select tab <tabid>
sidebar-select folder <folderid>
sidebar-select parent
```

#### `sidebar-activate`

Performs the sidebar `Enter` action on the current selection: activates a tab,
enters a folder, or follows `..` to the parent. Returns updated `sidebar` JSON.

#### `sidebar-search ...`

Controls the same global, case-insensitive folder/tab filter as the sidebar `/`
and `?` bindings without synthesizing keyboard input:

```text
sidebar-search forward <query>
sidebar-search backward <query>
sidebar-search next [same|opposite|forward|backward]
sidebar-search clear
```

Starting a search selects the nearest wrapped match in the requested direction.
`next` repeats the retained query, and `clear` reveals the selected result in its
containing folder just like sidebar `:noh`.

### Tab control

#### `tab-pin <tabid> [on|off]`

Pins or unpins a tab in its current sidebar folder without changing its stable
ID or reloading its browser backend. Omitting `on|off` toggles the current state.
A newly pinned tab is placed at the bottom of the combined pinned tab/folder
section; an unpinned tab is placed at the top of the regular section. The state
survives profile restarts and is exposed as `pinned` by `tabs`.

#### `tab-focus <tabid>`

Focuses a tab by stable ID. Returns `status` JSON.

#### `tab-delete <tabid>`

Deletes a tab by stable ID, using the same backend-destroying close path as the vim `d`/`D` bindings. Returns `status` JSON.

#### `tab-order <tabid> <zero-based-index>`

Moves a tab to a target zero-based index. The target is clamped into the valid range. The tab's ID is preserved and the active tab identity is preserved across the reorder. Returns `tabs` JSON.

#### `open-tab <url-or-query-or-local-path>`

Resolves the text with the same native URL/search/local-file path used by `:open`, records open history, opens a new active tab, and returns `status` JSON.

#### `open-context-tab <context-name> <url-or-query-or-local-path>`

Opens a new active tab in a named persistent `CefRequestContext`. A name must be
1-48 characters, start with a lowercase ASCII letter or digit, and contain only
lowercase ASCII letters, digits, `-`, and `_`. Each name maps to
`<CEF-root-cache>/contexts-<context-name>`; cookies (including session cookies),
local storage, IndexedDB, cache storage, and other CEF profile data therefore
persist across process restarts. The `contexts-` prefix keeps these profiles
visibly grouped while satisfying Chrome-runtime CEF's requirement that profile
cache paths be immediate children of the configured root cache. Different names
do not share request-context storage with each other or with the default profile.
Reusing a name shares the same context and storage.

Named-context tabs are deliberately **transient shell tabs**: vimbrowser keeps
their context data on disk but excludes their restorable tab entries from
session state and excludes them from the undo-close stack. This avoids the
legacy URL-only session format ever restoring an isolated URL in the default
context. Tabs created from an isolated tab (new-tab link actions, clones,
targeted links, and popups) retain the same context and are transient too.
Closing/restarting the browser does not delete the named context directory; open
it again with this command to continue using the persisted login/storage state.

Example:

```sh
scripts/vimbrowser-ipc open-context-tab work https://example.com/
scripts/vimbrowser-ipc open-context-tab personal https://example.com/
```

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

#### `key <[ctrl+][shift+][alt+][cmd+]key>`

Sends a synthetic key through vimbrowser's normal key handling path for the
active browser surface. This is intended for portable regression
tests and local automation. Examples: `key j`, `key shift+f`, `key ctrl+l`,
`key ctrl+shift+i`, `key escape`, and `key space`. Returns `status` JSON.

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

Reads JavaScript from a local regular file in the browser process and evaluates
it with the same renderer bridge as `js`. File I/O runs on CEF's blocking-file
thread instead of the UI thread. The file is limited to 1 MiB; terminals,
devices, FIFOs, and other non-regular paths (including `/dev/stdin`) are rejected
so a malformed automation command cannot hang the browser.

#### Exact frame inspection

`frame-tree <tabid>` returns the current primary frame tree with opaque CEF
frame identifiers, parentage, URL/name, depth, focus, and whether each child is
an out-of-process iframe. It does not activate or focus anything.

`frame-html <tabid> <frameid>`, `frame-text <tabid> <frameid>`, and
`frame-js <tabid> <frameid> <javascript>` are exact-frame counterparts of the
main-document commands. The browser resolves the identifier against that tab;
stale or cross-tab identifiers are rejected. This gives agent tooling explicit
OOPIF addressability rather than pretending every tab has only one document.

`inspect-controls <tabid> <base64-v1-json-query>` performs read-only native
inspection of click candidates in one exact frame. The query contains the frame
ID plus optional exact computed role/name and bounded surrounding-text filters.
It returns every match up to a strict limit with role/name/tag/type/id/text and
context metadata, but never form values, DOM node IDs, renderer process IDs, or
coordinates. Each match receives a random `eh1_...` handle valid for 15 seconds.
Inspection never chooses or activates the first match; clients should reject
zero or multiple results when they require one target.

Handles are browser-owned one-shot capabilities bound to the exact browser,
`GlobalRenderFrameHostToken`, `DocumentToken`, and renderer-private DOM node ID.
An identical replacement element cannot inherit a handle. Activation consumes
the capability before validation and rejects expiration, replay, frame/document
navigation, node removal/replacement, disabling, visibility changes, local hit
changes, and OOPIF compositor-target mismatches. The internal activation point
is selected by Blink and never crosses the public IPC boundary.

#### `upload-file <tabid> <base64-v1-json-payload>`

Securely assigns explicit local files to one `<input type=file>` or atomically
native-activates one custom Browse control and supplies the chooser it opens.
The payload is compact JSON encoded with standard base64 so CSS selectors and
paths are not corrupted by the protocol's whitespace framing:

```json
{
  "version": 1,
  "target": {"kind": "css", "value": "#attachment"},
  "paths": ["/home/me/report.pdf"]
}
```

`target.kind` is either:

- `css`: `value` is a CSS selector which must match exactly one element
- `index`: `value` is the explicit zero-based index among
  `input[type="file"]` elements in the main document
- `activate`: `value` is a CSS selector which must match exactly one visible
  element in the main document. Chromium resolves it in Blink, registers the
  pending upload first, native-activates the element with trusted mouse input,
  and supplies the open-file chooser produced by that activation. The IPC reply
  is held until the chooser is consumed or the short activation deadline
  expires; no platform file-picker window is shown.
- `handle`: `value` is a short-lived exact-node capability returned by
  `inspect-controls`. This is the preferred path for controls inside
  cross-origin/OOPIF documents. Chromium revalidates the stored frame,
  document, node, local hit target, and compositor hit target before dispatch.
  A handle is one-shot even if activation fails.
- `chooser`: arms the next browser-native open-file chooser request from the
  specified tab for 60 seconds; this target has no `value`

CSS targets with zero or multiple matches fail; there is no fallback to another
input. An index is only appropriate when the caller controls or otherwise knows
the page's input ordering. In either mode, the resolved node is described again
and rejected unless it is actually an `<input type=file>`.

The browser decodes the payload, requires 1–32 absolute paths, canonicalizes each
path on its blocking-file thread, opens it nonblocking, and verifies it is a
readable regular file. It rejects multiple paths when the input lacks
`multiple`, and checks `accept` extension/MIME constraints when the MIME type can
be inferred from a filename extension. It then assigns the canonical paths using
CEF's trusted browser-process DevTools connection and
`DOM.setFileInputFiles`. Local file contents are never read by injected page
JavaScript or included in IPC responses.

Successful and command-specific error responses are structured JSON. Responses
report file counts and input constraints but never include local paths, names, or
file contents:

```json
{"ok":true,"tabid":3,"file_count":1,"target":{"kind":"css","match_count":1},"input":{"multiple":false,"accept":"application/pdf"}}
```

Clients should normally use `vimbrowser-cli upload-file`, which performs an
additional path check and creates the versioned payload. Dynamic-site callers
should wait for the intended input to exist and use a site-specific unique CSS
selector rather than retrying with broader selectors.

For a custom Browse button that creates an ephemeral input or calls the File
System Access API from its click handler, use atomic native activation:

```sh
vimbrowser-cli upload-file @active \
  'activate:#browse-resume' /absolute/path/resume.pdf
```

For a cross-origin picker, inspect rather than guessing:

```sh
vimbrowser-cli frame-tree @active --pretty
vimbrowser-cli inspect-controls @active --frame FRAME_ID \
  --role button --name-exact Browse \
  --context-contains 'Upload files' --require-one --pretty
vimbrowser-cli upload-file @active \
  'handle:eh1_HANDLE_FROM_INSPECTION' /absolute/path/resume.pdf
```

The handle upload arms the chooser before activating the exact stored node and
uses the same causal nonce as main-document atomic activation. An unrelated
chooser cannot consume the files. Same-process child-frame activation fails
closed when Chromium cannot independently prove the ancestor compositor target;
OOPIFs use Chromium's input-event router for that proof.

This is a single IPC transaction. The customized Chromium backend resolves one
visible selector match in the current main document, verifies that the native
hit test reaches that element or one of its flat-tree descendants, and activates
that verified point through Blink's native mouse event path. This establishes the
transient user activation required by file pickers without using page JavaScript.
Covered, pointer-inert, transparent, and stale-document targets are rejected
before dispatch.

The browser generates an unguessable nonce for the operation and scopes it to the
synchronous native activation. Blink copies that nonce into either a regular
`<input type=file>` request or File System Access picker metadata, carries it
through Chromium's chooser pipeline, and exposes it to CEF only during the
corresponding `OnFileDialog` callback. Vimbrowser supplies paths only when the
nonce and stable browser instance match; an unrelated same-tab chooser cannot
consume the arm, and additional chooser requests from the activation are
canceled instead of opening native UI. The chooser is intercepted before native
UI is created and completed with `CefFileDialogCallback::Continue` after path
revalidation.

Zero or multiple selector matches, invisible or obscured targets, navigation,
wrong dialog modes, constraint failures, deadline expiry, and controls that do
not open a chooser all return structured errors. There is deliberately no
fallback from a failed direct-input assignment to element activation because
activation can have arbitrary page side effects.

For upload controls that do not expose a persistent file input, use the explicit
manual chooser target when a human should choose the control:

```sh
vimbrowser-cli upload-file @active chooser /absolute/path/resume.pdf
# Within 60 seconds, the user clicks the intended "Browse" button in that tab.
vimbrowser-cli upload-file-status @active --pretty
```

The arm is one-shot and tied to the stable tab ID and browser instance. Only one
chooser upload can be armed in a window at once. It is canceled by main-frame
navigation, tab closure, explicit `upload-file-cancel`, or expiration. It accepts
only open/open-multiple requests—folder and save dialogs are never supplied with
the armed paths. At chooser time the browser checks the actual CEF dialog mode,
`accept` filters, and single/multiple constraint, then repeats regular-file path
validation on the blocking-file thread before calling
`CefFileDialogCallback::Continue`. A mismatch cancels that chooser rather than
falling through to a different target.

This path is implemented by `CefDialogHandler::OnFileDialog`, so it covers both
ephemeral inputs created only during a trusted click and Chromium File System
Access pickers such as `showOpenFilePicker`. It does not synthesize the click:
arming paths and choosing the intended page control remain separate explicit
actions. Status responses expose only state/count/mode and structured error
codes, never paths or file contents.

#### `upload-file-status <tabid>`

Returns `none`, `armed`, `validating`, `consumed`, `failed`, `expired`, or
`canceled` state for the chooser target, plus the non-sensitive file count,
remaining arm lifetime, and observed dialog mode.

#### `upload-file-cancel <tabid>`

Explicitly cancels an `armed` or `validating` chooser upload. Any chooser
callback already being validated is canceled instead of receiving files.

Focused end-to-end coverage uses a controlled local form and its own temporary
profile. It must only be launched on a nested X11 display:

```sh
xenv start vimbrowser-upload-test
xenv exec -e vimbrowser-upload-test env VIMBROWSER_E2E_NESTED_X11=1 \
  python3 tests/upload_file_e2e.py \
  --binary "$PWD/build-source/Release/vimbrowser" \
  --cli /home/yeyito/Workspace/exocortex/external-tools/vimbrowser-cli/bin/vimbrowser-cli \
  --xenv-instance vimbrowser-upload-test
xenv stop vimbrowser-upload-test
```

The test does not use host-display input automation, does not connect to the
normal profile, and terminates only the isolated process group it creates.

#### `cookies-url <url>`

Atomically lists cookies visible for an explicit URL using the profile's global
backend `CefCookieManager`, including HttpOnly cookies. This form does not
require, focus, load, or mutate any tab and is preferred for external tools that
only need site cookies:

```json
{"cookies":[{"name":"sid","value":"...","domain":"example.com","path":"/","secure":true,"httponly":true,"same_site":"lax","creation":0,"last_access":0,"has_expires":false,"expires":0}]}
```

#### `cookies <tabid> [url]`

Lists cookies visible for the tab URL, or for an explicit URL when `url` is
provided, using the tab's backend `CefCookieManager`, including HttpOnly
cookies. This form is kept for tab-scoped inspection and for compatibility:

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
