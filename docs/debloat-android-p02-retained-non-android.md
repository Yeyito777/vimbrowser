# P02 retained non-Android implementations

This is a retention checkpoint, not a deletion checkpoint. It prevents the
residual Android sweep from misclassifying desktop implementations merely
because Chromium named them `*_non_android*` or `*nonandroid*`.

## Inventory

Ten tracked files match those naming conventions:

- four extension API/host implementations plus the tabs delegate header;
- one desktop extension browser-client implementation;
- one extension browser-test platform delegate;
- three browser-window collection/creation implementations.

Eight source files are direct inputs to the Linux `libcef.so` production build:

- `chrome_extensions_api_client_non_android.cc`;
- `chrome_management_api_delegate_nonandroid.cc`;
- `tabs_event_router_platform_delegate_non_android.cc`;
- `chrome_extension_host_delegate_non_android.cc`;
- `chrome_extensions_browser_client_non_android.cc`;
- `browser_window_interface_iterator_non_android.cc`;
- `create_browser_window_non_android.cc`;
- `global_browser_collection_platform_delegate_non_android.cc`.

The tabs delegate header supports one of those inputs. The remaining
`extension_browsertest_platform_delegate_non_android.cc` is test-only and is
retained with the surviving relevant extension tests.

## Decision

All ten files remain. In Chromium terminology, “non-Android” means the desktop
implementation selected by the `else` branch when Android is not the target; it
does **not** mean an obsolete compatibility shim. The browser-window files are
part of retained tabs/windows behavior. The extension files support the thin
extension skeleton still required by CEF and will be reconsidered only during
the dedicated extension sweep.

Likewise, this checkpoint makes no opportunistic changes to cross-platform web
payment code or Rust sources/toolchains found while classifying Android-bearing
paths. Chrome payment/autofill products are owned by stage A, while Rust source
and toolchain reduction is owned by P04/G06. Keeping those boundaries prevents
name-based deletion from breaking ordinary web APIs or clean builds.

## Validation

The preceding isolated worker build for tree
`3e5eedcd6333c6fba9a7ceb3dfc937139878e052` compiled all eight production
sources, passed CTest, and produced the checksummed runtime that passed the local
benchmark. `ninja -t inputs libcef.so` records the eight source inputs and their
objects. No source changes are made by this retention checkpoint, so no new
binary is produced. The installed stable runtime remains byte-for-byte
unchanged, and the Google Cloud worker remains stopped.
