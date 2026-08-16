# P03 residual platform pruning: iOS desktop integrations

This checkpoint continues P03 from `4f5f33e833a41516e17d9a55c55570aa3538b827`
without changing the installed or running stable browser. It disconnects and
physically removes iOS-only promotion, push-delivery, and segmentation-model
integration that was still compiled through shared Linux desktop targets.

## Scope removed

- Removed the desktop-to-iOS promotion feature end to end: browser services,
  promo state and prefs, autofill/password/Lens/bookmark/Safety Hub entry
  points, Views bubbles, user-education registration, colors, flags, tests,
  resources, metrics, and the generic desktop-to-mobile promo component that
  had no surviving user.
- Removed iOS Chime push delivery from Sharing: the sender and utilities,
  unencrypted Send Tab and desktop-promo payloads, delegates, service/factory
  integration, metrics, tests, and the Chime representative target stored in
  synced device information.
- Removed the iOS default-browser and start-page module-ranking segmentation
  models, their rank-fetcher paths, feature flags, tests, metrics, and model
  quality payload.
- Removed BoringSSL's iOS random implementation and its generated source-list
  entries.

The ordinary encrypted FCM Sharing path and Send Tab to Self model remain.
This preserves desktop Send Tab behavior while eliminating only the extra
iOS-specific unencrypted push side effect. The shared sync protocols also keep
their old field numbers and names reserved so stale data cannot be reinterpreted
as future fields.

The backend checkpoint changes 186 files, physically deletes 53 files, removes
9,793 lines, and adds 105 lines, for a net reduction of **9,688 lines**. Deleted
Git blobs account for **310,855 bytes** (0.296 MiB).

## Validation

Code snapshot tree `fbdf155f6eca7ebf484d1de502c0aca9b3045f83` passed on the
isolated Google Cloud worker:

- forced Chromium/CEF GN generation: 27,166 targets from 4,265 files;
- complete `libcef` build;
- clean vimbrowser shell configuration and build;
- CTest: 1/1 passed;
- checksummed runtime archive creation, fetch, extraction, and per-file
  verification;
- direct compilation of eight affected surviving unit-test translation units
  for Sharing and synced device information.

The full aggregate unit-test target is still blocked by the intentionally
removed Android Perfetto SQL corpus in its shared test-only input bundle, so the
affected translation units were compiled directly against the generated
production tree instead. All eight compiled successfully.

The fetched artifact is at:

`/home/yeyito/Workspace/vimbrowser-debloat-artifacts/fbdf155f6eca7ebf484d1de502c0aca9b3045f83/Release`

Artifact measurements:

- runtime tree: 363,489,163 bytes;
- compressed archive: 146,215,142 bytes;
- stripped `vimbrowser`: 2,032,456 bytes, SHA-256
  `8a038f32c6d53e69e8cb5998853f567d253309ba190aacf80d51ca7c96f53b1d`;
- stripped `libcef.so`: 309,074,912 bytes, SHA-256
  `21a4ee53d509a3190bd882fa5c6e501f0e42773294312db431eacad02680881a`.

Compared with the preceding P03 checkpoint, `libcef.so` and the runtime tree
are 126,976 bytes smaller, the compressed archive is 19,835 bytes smaller, and
the vimbrowser executable is byte-identical. GN generation dropped ten targets
and four input BUILD files.

The exact fetched binary also passed `make benchmark ...` with `CHECK: PASS`,
covering startup, session restore, page/resource loading, tab switching,
screenshots, background throttling, notification denial, and retained
service-worker behavior.

`scripts/debloat-sandbox.sh verify` confirmed throughout that
`build-source/Release` remained byte-for-byte unchanged. No artifact was
installed, the user's running stable browser was not restarted, and the cloud
worker is stopped.
