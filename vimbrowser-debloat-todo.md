# Vimbrowser Chromium Debloat TODO

This is the ordered, long-horizon checklist for turning the Chromium/CEF fork
into the smallest maintainable backend that still provides the functionality
required by vimbrowser. Work should proceed from top to bottom unless a later
item is explicitly pulled forward because it is a prerequisite.

Do not treat a subsystem as complete merely because a GN flag disables it.
Completion normally means disconnecting it from the production graph, deleting
its implementation/resources/tests, regenerating GN successfully, building,
installing, smoke testing retained behavior, measuring the result, and then
committing and pushing the stage.

## Status and recovery anchors

- [x] Stable cleanup anchor is `b2aded483e8868e8e2be29c1afee3407d5d1701a`
      (`Remove page translation support`).
- [x] Stable anchor is pushed on `main` and tagged history includes
      `pre-cleanup`.
- [x] The browser installed during notification validation was built from the
      finished notification worktree and passed the local integration benchmark.
- [x] Isolate continued debloat work from the normal installed/running browser.
  - Stable runtime remains at `build-source/Release` and is checksum-guarded.
  - Chromium builds use `backend/chromium/out/Debloat_GN_x64`.
  - CEF, shell build, install prefix, profile, and IPC socket live under
    `/home/yeyito/Workspace/vimbrowser-debloat-sandbox`.
  - `scripts/debloat-sandbox.sh` is the required build/install/test wrapper.
  - Sandbox `make install` never writes `~/.local/bin` and no promotion command
    exists; stable promotion is a separate explicit decision.
  - Baseline sandbox `make`, sandbox `make install`, CTest, and local benchmark
    passed; representative GN/runtime files have distinct inodes, and the
    stable runtime checksum remained unchanged after every operation.
- [x] Finish the stashed notification/Message Center presentation cleanup.
  - Recovery source was the immutable stash commit
    `e9ce063757d4f169d690e62a0e8c706bec13bad9`.
  - The original 92-file draft grew into a physical 519-file cleanup after its
    Chrome, platform-bridge, scheduler, resource, and Safe Browsing dependencies
    were traced and disconnected.
  - The original named stash may remain locally as a pre-commit recovery copy;
    the validated implementation is committed on `main` and pushed.

## Product contract: functionality that must survive

- [x] Keep normal web browsing, navigation, tabs, windows, history, and
      bookmarks. These may be simplified later, not removed.
- [x] Present ordinary web popups as tabs while preserving opener/WindowProxy
      semantics; retain a native floating window only for document
      Picture-in-Picture.
- [x] Keep downloads.
- [x] Keep uploads and trusted file chooser support.
- [x] Keep cookies, localStorage, IndexedDB, Cache Storage, and other ordinary
      site storage needed by modern websites.
- [x] Keep service workers.
- [x] Keep audio/video playback and fullscreen.
- [x] Keep PDF viewing while removing printing and print preview.
- [x] Keep DevTools.
- [x] Keep camera, microphone, and WebRTC.
- [x] Keep proprietary codecs needed by sites such as X/Twitter and Steam:
      H.264, AAC, and Chrome FFmpeg branding.
- [x] Keep Widevine/DRM support for now.
- [x] Keep PWA/web-app behavior for now; revisit only at the explicit decision
      gate below.
- [x] Keep Linux x86-64 with X11 as the primary platform.
- [x] Keep macOS support.
- [x] Avoid blocking a future Linux X11 ARM/ARM64 build when pruning host and
      target architecture support.
- [x] Do not plan for Chromium rebases; security patches may be applied manually.

## Product contract: functionality approved for removal

- [x] Remove passwords and password-manager functionality.
- [x] Remove autofill.
- [x] Remove printing and print preview.
- [x] Remove website presentation, Chrome popup, and native OS notifications
      while retaining the denied extension compatibility stub and independently
      useful service-worker/push infrastructure. Residual producer code is
      explicitly tracked below with its owning subsystem.
- [ ] Remove most extension functionality; retain only the smallest skeleton
      required by CEF/shared Chromium interfaces.
- [ ] Remove multiple-profile, guest, and incognito/off-the-record behavior;
      retain one persistent vimbrowser profile.
- [x] Remove spellcheck.
- [ ] Remove accessibility support.
- [x] Remove page translation and on-device translation.
- [ ] Remove WebAuthn/passkeys.
- [ ] Remove WebUSB, Web Bluetooth, Web Serial, and Web MIDI.
- [ ] Include WebHID and browser device-notification UI in the hardware-API
      removal unless a retained feature proves it is needed.
- [x] Remove Wayland, iOS, Windows, ChromeOS, Fuchsia, Android product builds,
      Cast product builds, official Google Chrome packaging, and unrelated
      cross-compilation support. Residual embedded platform code still needs a
      second sweep below.

## Completed broad-sweep commits

- [x] `67a897b3a5` — remove CEF test support from the production library.
- [x] `e854837424` — remove unsupported platform product trees.
- [x] `32b89f4fa7` — remove the Wayland backend while retaining X11/headless
      Ozone support needed by CEF.
- [x] `d1303491e8` — remove Windows platform support.
- [x] `16dfdc637e` — remove ChromeOS platform support.
- [x] `e1e1437b58` — remove unused Blink test corpora.
- [x] `8d9729408c` — remove standalone remoting and standalone headless products.
- [x] `9fc7bd6a83` — remove browser printing support while retaining PDF viewing.
- [x] `295574f706` — remove spellcheck and Hunspell support.
- [x] `7d9a21bce0` — remove on-device translation support.
- [x] `b2aded483e` — remove Chrome page-translation support while retaining the
      low-level language detector still required by surviving code.

## Current measurements

These are reference measurements, not final acceptance values.

- [x] Record pre-cleanup source LOC: approximately 99,156,931.
- [x] Record current stable source LOC: approximately 80,193,629.
- [x] Record current net source reduction: approximately 18,963,302 lines
      (19.1%).
- [x] Record pre-cleanup tracked files: approximately 905,666.
- [x] Record current tracked files: approximately 631,738, about 30.2% fewer.
- [x] Record pre-cleanup tracked bytes: approximately 10,356,855,801.
- [x] Record current tracked bytes: approximately 8,175,327,078, about 21.1%
      fewer.
- [x] Record original clean-build baseline: approximately 298 minutes 22
      seconds.
- [x] Record recent stable no-op/incremental build: approximately 12.4 seconds.
- [x] Record current stable artifacts: Chromium out directory approximately
      7.81 GB; installed runtime approximately 364.6 MB; unstripped `libcef.so`
      approximately 485.6 MB; shell approximately 2.08 MB.
- [x] Record notification-stage delta before adding this checklist: 519 files
      changed, 470 files deleted, 205 insertions, 74,553 deletions, net 74,348
      lines removed, and approximately 2.67 MiB of tracked source blobs deleted.
- [x] Record notification-stage artifacts: Chromium out approximately 7.82 GB;
      installed runtime 364,113,199 bytes; unstripped `libcef.so` 484,823,496
      bytes; shell 2,079,392 bytes.
- [ ] Run and record the first post-broad-sweep clean build after notification
      cleanup and the next safe checkpoint.

# Sequential remaining work

## N. Finish notification presentation removal — COMPLETE

- [x] **N01:** Apply the notification draft by immutable ID without dropping the
      backup stash: `git stash apply e9ce063757d4f169d690e62a0e8c706bec13bad9`.
- [x] **N02:** Confirm the applied diff exactly matches the recorded 92-file
      Message Center draft before editing further.
- [x] **N03:** Keep `enable_chrome_notifications` and `enable_message_center`
      permanently false for every retained platform; do not leave a supported
      override that refers to physically deleted source.
- [x] **N04:** Finish replacing `ui/message_center` with a null process-global
      compatibility shell.
  - Retain only the minimal `MessageCenter::Initialize/Get/Shutdown` shell.
  - Retain `ui/message_center/public/cpp` notification value types while media
    controls or shared interfaces use them.
  - Delete popup views, center views, timers, lock-screen integration, vector
    icons, implementation tests, and implementation-only metrics.
- [x] **N05:** Remove all live production includes of deleted Message Center headers.
  - Persistent and non-persistent notification-handler statistics hooks were
    already removed in the stash.
  - Verify remaining references are dead conditional code or optional tests,
    then delete those callers instead of restoring the implementation.
- [x] **N06:** Remove Chrome popup fallback sources and their GN branches:
      notification UI manager, popup controllers, fullscreen/screen-lock
      blockers, Message Center platform bridge, and system observer.
- [x] **N07:** Disable Web Notifications at the embedder boundary.
  - Make retained `BrowserContext`/profile implementations return no
    `PlatformNotificationService` for website notifications.
  - Ensure Content reports notification permission as denied.
  - Ensure notification requests never create a prompt.
- [x] **N08:** Simplify or replace `NotificationPermissionContext` with a
      deterministic denied implementation, then remove it if Content no longer
      needs a registered context.
- [x] **N09:** Replace Chrome's internal `NotificationDisplayServiceImpl` with a
      minimal no-op compatibility service only where retained callers still
      require its interface.
- [ ] **N10 (cross-stage residual):** Physically remove remaining internal Chrome
      producer code rather than relying forever on the no-op display boundary.
      Announcement, Safe Browsing content-detection/telemetry, and the central
      handlers are gone. Producers coupled to downloads, extensions, hardware,
      sharing, PWAs, enterprise policy, default-browser/update UI, and capture
      indicators remain non-presenting and must be deleted with those owning
      subsystem stages so retained behavior can be tested coherently.
  - Download-complete/failure notifications.
  - Update and announcement notifications.
  - Default-browser notifications.
  - Sharing, shared-clipboard, and send-tab notifications.
  - Safe Browsing tailored-security notifications.
  - Web-app relaunch/run-on-login notifications.
  - Enterprise request notifications.
  - USB/HID/device notifications.
  - Screen/multi-capture notifications, while preserving necessary camera,
    microphone, and WebRTC capture indicators.
- [x] **N11:** Remove Linux D-Bus/native notification bridge code.
- [ ] **N12 (macOS follow-up):** Remove the final macOS notification mojom/action
      compatibility utilities and helper-app packaging after a retained-PWA macOS
      build is available. The native platform bridge, provider factory,
      dispatcher, and app-shim termination callback are already removed.
- [x] **N13:** Remove `NotificationPlatformBridge` ownership/accessors from
      `BrowserProcess` after all retained callers are gone.
- [x] **N14:** Remove notification scheduling, background notification tasks,
      scheduler proto databases, and scheduler resources.
- [x] **N15:** Remove Safe Browsing notification-content detection while
      retaining core Safe Browsing until its separate decision gate.
- [x] **N16:** Deny the extension notification API and remove its Chrome
      presentation dependencies. The generated API/handler compatibility stub is
      intentionally deferred to the broader extension graph removal in stage E.
- [x] **N17:** Decide the smallest safe fate of Blink's Notifications module and
      Content notification storage/proto code.
  - Prefer physical removal of the website-facing API.
  - Retain a thin protocol/storage skeleton only if independently required by
    service-worker or push infrastructure.
  - Do not remove service-worker registration, execution, storage, or update
    machinery.
  - Decision: retain Blink's interface and Content's notification database/proto
    skeleton because they are coupled to retained service-worker and push
    plumbing; Chrome exposes no platform service and always denies permission.
- [x] **N18:** Keep `components/media_message_center` initially because it is
      global media-control UI, not ordinary website notifications; reevaluate it
      only with audio/video tests in hand.
- [x] **N19:** Add an integration test proving `Notification.permission` is
      denied and `Notification.requestPermission()` does not prompt.
- [x] **N20:** Add or retain integration coverage proving service workers and
      push plumbing still initialize after website notifications are removed.
- [x] **N21:** Regenerate GN and record notification-related labels remaining in
      the `//cef:libcef` production closure.
  - Chrome: `//chrome/browser/notifications:notifications`,
    `:system_notification_helper`, and `:system_notification_helper_impl`.
  - Shared compatibility: `//chrome/common/notifications:notifications`,
    `//ui/message_center:message_center`, and `//ui/message_center/public/cpp:cpp`.
  - Retained web plumbing: Content notification proto plus Blink's Notifications
    module. Scheduler, native bridge, announcement, notification-internals, and
    Safe Browsing notification-detection labels are absent.
- [x] **N22:** Run `make`, `make install`, CTest, the retained-feature local smoke
      suite, and benchmarks.
  - `make` and `make install` passed; CTest passed 1/1.
  - The local benchmark passed all thresholds and explicitly observed
    `Notification.permission == "denied"`, `requestPermission() == "denied"`,
    no prompt timeout, and successful service-worker registration.
  - The unrelated key-routing regression suite was also attempted and failed at
    its first pre-existing mode-entry assertion (`WEBSITE` instead of `INSERT`);
    no notification, Message Center, or backend presentation code is involved.
- [x] **N23:** Commit and push the validated notification removal before moving
      to stage P.

## P. Residual unsupported-platform and product sweep

- [x] **P01:** Inventory remaining Android-only directories embedded under
      `chrome/browser`, `content`, `components`, `media`, `services`, `ui`,
      `third_party`, `tools`, and `build`.
  - The checkpoint inventory and reproducible classification are recorded in
    `docs/debloat-android-inventory.md` against commit `f27836adf8`.
  - 532 Android-bearing roots contain 23,276 files and approximately 2.58
    million lines; 520 roots/22,273 files have no descendant production target
    or tracked source input in the Linux `libcef` graph.
  - Twelve active directory exceptions, 2,825 standalone Android-named files,
    35 additional Chromium-owned Java/JNI roots, and 3,565 Android-referencing
    build/metadata files are classified explicitly rather than guessed from
    names.
- [ ] **P02:** Remove remaining Android browser/product Java, JNI, resources,
      tests, build rules, manifests, and packaging after disconnecting every
      retained GN reference.
  - [ ] Delete the 520 production-disconnected Android-bearing roots and 35
        Chromium-owned generic Java/JNI roots from the P01 checkpoint.
  - [ ] Remove their GN/GNI/resource/test/manifest/packaging branches instead of
        leaving unsupported `is_android` paths.
  - [ ] Disconnect WebAPK, desktop remote-Android DevTools, Blink Android-font
        mojom, and Android-only entries in shared Skia/ANGLE/GL/media lists.
  - [ ] Remove Android-only Perfetto/Catapult schemas, importers, and trace
        resources from their cross-platform aggregates.
  - [ ] Retain `*_non_android*` implementations; defer cross-platform web
        payment code to stage A and Rust target/toolchain source pruning to P04.
- [ ] **P03:** Remove residual iOS, Windows, ChromeOS, Fuchsia, Cast, and Wayland
      branches/files that survived because they are embedded in shared
      directories.
- [ ] **P04:** Remove obsolete platform toolchains and SDK/download declarations
      only after Linux x86-64 and macOS GN generation no longer parse them.
- [ ] **P05:** Retain Linux ARM/ARM64 CPU definitions and architecture-neutral
      code needed for a possible future X11 ARM build.
- [ ] **P06:** Remove official Google Chrome branding, installer, updater,
      enterprise packaging, symbols/upload, and release-channel machinery not
      needed by vimbrowser.
- [ ] **P07:** Audit `DEPS`, CIPD declarations, gclient hooks, bootstrap scripts,
      and downloaded tool/data packages; physically remove unsupported-platform
      dependencies.
- [ ] **P08:** Validate Linux and macOS configuration assumptions before
      committing. Run a real macOS build at the next available opportunity.
- [ ] **P09:** Build, install, smoke test, measure, commit, and push the residual
      platform sweep.

## H. WebAuthn and hardware/device Web APIs

- [ ] **H01:** Remove WebAuthn/passkeys from Blink, Content, Chrome permissions,
      profile services, UI, settings, resources, and tests.
- [ ] **H02:** Remove password-manager/WebAuthn coupling rather than retaining
      one subsystem merely to satisfy the other.
- [ ] **H03:** Remove WebUSB implementation, permission contexts, chooser UI,
      internals pages, device observers, notification/status icons, resources,
      and tests.
- [ ] **H04:** Remove Web Bluetooth implementation, permission contexts, chooser
      UI, internals pages, metrics, resources, and tests.
- [ ] **H05:** Remove Web Serial implementation, permission contexts, chooser UI,
      internals pages, policies, resources, and tests.
- [ ] **H06:** Remove Web MIDI implementation and permissions while preserving
      ordinary audio playback and WebAudio unless separately approved.
- [ ] **H07:** Remove WebHID implementation, permissions, pinned notifications,
      UI, policies, and tests unless a retained input feature proves it is
      required.
- [ ] **H08:** Audit NFC, Direct Sockets, sensors, gamepad, and other device APIs;
      place each behind a user decision before removal if it was not explicitly
      approved above.
- [ ] **H09:** Prove camera, microphone, screen capture, audio playback, video
      playback, fullscreen, and WebRTC still work.
- [ ] **H10:** Build, install, smoke test, measure, commit, and push the hardware
      API sweep.

## A. Password manager, autofill, payments, and form-assistance products

- [ ] **A01:** Map password/autofill targets in the production CEF closure and
      separate ordinary HTML form behavior from Chrome assistance products.
- [ ] **A02:** Remove password storage, generation, leak checks, compromised
      credential services, password UI, settings, sync bridges, actors, metrics,
      resources, and tests.
- [ ] **A03:** Remove address, contact, and credit-card autofill services, form
      parsing/filling UI, settings, sync bridges, actors, metrics, resources,
      and tests.
- [ ] **A04:** Remove payment autofill, virtual cards, card unmasking, BNPL,
      offers, save-card UI, and related optimization-guide models.
- [ ] **A05:** Remove plus-address, compose/form-writing assistance, and related
      account/profile integrations unless separately retained.
- [ ] **A06:** Preserve native HTML inputs, forms, keyboard editing, clipboard,
      file inputs, JavaScript form APIs, and browser submission/navigation.
- [ ] **A07:** Add form regression coverage: typing, selection, paste, submit,
      autocomplete attributes ignored safely, file upload, and login forms
      operating without password capture.
- [ ] **A08:** Build, install, smoke test, measure, commit, and push the
      password/autofill sweep.

## E. Extensions reduced to a CEF compatibility skeleton

- [ ] **E01:** Map every direct CEF dependency on `//extensions`, including
      generated headers, GRIT resources, MIME/plugin plumbing, and extension
      schemes.
- [ ] **E02:** Define the exact minimal extension interfaces CEF still needs.
- [ ] **E03:** Remove Chrome extension installation, update, stores, management,
      developer mode, toolbar UI, settings, APIs, service workers, permissions,
      content scripts, resources, and tests not required by that interface.
- [ ] **E04:** Remove built-in component extensions that duplicate retained
      native functionality, while preserving PDF viewing and DevTools through
      their actual required paths.
- [ ] **E05:** Replace unavoidable extension dependencies with small native C++
      compatibility targets instead of retaining monolithic extension graphs.
- [ ] **E06:** Confirm PDF and DevTools function with no general-purpose extension
      installation or execution capability.
- [ ] **E07:** Build, install, smoke test, measure, commit, and push the extension
      skeleton.

## R. Collapse profiles, guest, and incognito to one persistent profile

- [ ] **R01:** Document the one retained profile directory and startup lifecycle.
- [ ] **R02:** Remove profile picker, profile creation, switching, avatars,
      account-profile separation, guest mode, supervised profiles, and profile
      management UI.
- [ ] **R03:** Remove off-the-record/incognito implementations and every call path
      that creates an incognito browser context.
- [ ] **R04:** Simplify profile selections in keyed-service factories to one
      regular persistent instance.
- [ ] **R05:** Remove incognito-specific preferences, policy allowlists, storage
      partitions, tests, resources, commands, and menus.
- [ ] **R06:** Preserve ordinary cookie/storage clearing and per-site data
      controls without relying on incognito machinery.
- [ ] **R07:** Verify startup, session restoration, cookies, IndexedDB, service
      workers, downloads, history, bookmarks, and DevTools under the single
      profile.
- [ ] **R08:** Build, install, smoke test, measure, commit, and push the profile
      collapse.

## X. Accessibility removal

- [ ] **X01:** Map accessibility targets in the CEF production closure and divide
      essential DOM/layout semantics from browser/platform assistive UI.
- [ ] **X02:** Remove Linux ATK/AT-SPI platform integration and optional braille,
      screen-reader, accessibility-tree inspection, and accessibility UI.
- [ ] **X03:** Remove macOS accessibility bridge code after validating that normal
      window/input behavior does not depend on it.
- [ ] **X04:** Remove Chrome accessibility features, settings, resources, tests,
      annotations, Screen AI, read-aloud, and accessibility-specific services.
- [ ] **X05:** Minimize Blink/Content accessibility trees and Mojo interfaces as
      far as ordinary rendering, focus, selection, DevTools, and automation
      compatibility permit.
- [ ] **X06:** Decide separately whether live captions are retained as a media
      feature or removed as accessibility/product UI.
- [ ] **X07:** Verify keyboard input, focus, selection, text editing, tabs,
      DevTools inspection, and ordinary page rendering after each cut.
- [ ] **X08:** Build, install, smoke test, measure, commit, and push accessibility
      removal.

## W. PWA and background web-app decision gate

- [ ] **W01:** Keep current PWA/web-app behavior unchanged through earlier stages.
- [ ] **W02:** Inventory installable web-app behavior, manifests, icons, OS
      integration, protocol/file handlers, run-on-login, app service, updates,
      isolated web apps, background apps, and PWA-specific UI.
- [ ] **W03:** Ask the user which PWA behaviors are actually wanted before
      deleting any of them.
- [ ] **W04:** At minimum, remove background-app/run-on-login/OS-integration
      machinery that receives explicit approval while preserving retained
      service-worker behavior.
- [ ] **W05:** Build, install, smoke test, measure, commit, and push any approved
      PWA simplification.

## D. Additional high-value product decision gates

Nothing in this section should be removed merely by assumption. Present closure,
size, coupling, and likely web-compatibility impact to the user first.

- [ ] **D01:** Decide removal of Glic, actor/automation product features, Compose,
      tab organization, optimization-guide models, and other Chrome AI features.
- [ ] **D02:** Decide removal of shopping, commerce, discounts, price tracking,
      product specifications, offers, and non-form payment product UI.
- [ ] **D03:** Decide removal of Media Router, casting, DIAL, presentation-device
      discovery, and mirroring while preserving local media and WebRTC.
- [ ] **D04:** Decide removal of Nearby Share, shared clipboard, send-tab-to-self,
      and Chrome sharing hubs.
- [ ] **D05:** Decide removal of enterprise management, cloud policy, reporting,
      connectors, device trust, and managed-profile machinery while retaining
      only security/network policy genuinely needed locally.
- [ ] **D06:** Decide removal or reduction of sign-in, Google account, sync,
      identity, and account-consistency services. History and bookmarks must
      remain locally even if sync is removed.
- [ ] **D07:** Keep core Safe Browsing by default; separately review download URL
      checks, tailored security, account-enhanced protection, reporting, and
      model-backed extras.
- [ ] **D08:** Decide removal of Privacy Sandbox product APIs: Topics,
      Attribution Reporting, Protected Audience/interest groups, private
      aggregation, and related settings/services.
- [ ] **D09:** Decide removal of FedCM and other identity-provider browser UI based
      on desired website login compatibility.
- [ ] **D10:** Decide removal of VR/AR/OpenXR and immersive UI.
- [ ] **D11:** Decide removal of crash upload, UMA/UKM, field trials, variations,
      user surveys, promo/feature-engagement, and telemetry while retaining
      local diagnostics useful to vimbrowser development.
- [ ] **D12:** Decide removal of first-run, default-browser prompts, Chrome
      whats-new/help, feedback, updater UI, and promotional surfaces.
- [ ] **D13:** Decide removal of reading mode, read-anything, live caption, speech
      recognition, text-to-speech, and other assistive/content products not
      already handled by accessibility removal.
- [ ] **D14:** Audit geolocation, sensors, gamepad, screen orientation, NFC,
      contacts, idle detection, direct sockets, and other optional Web APIs;
      request approval before removing unspecified capabilities.
- [ ] **D15:** Turn each approved decision into its own buildable, measurable,
      committed broad-sweep stage.

## T. Tests, examples, corpora, and developer-only source

- [ ] **T01:** Enumerate every test/example/fuzzer/benchmark target still reachable
      from production CEF and disconnect test-only dependencies.
- [ ] **T02:** Delete tests belonging exclusively to removed features rather than
      leaving a permanently broken Chromium test tree.
- [ ] **T03:** Delete unused web-test corpora, WPT expectations, test servers,
      fuzzers, screenshots, goldens, perf pages, and test data after proving the
      retained smoke suite does not use them.
- [ ] **T04:** Remove Chrome example/sample applications and developer utilities
      not used to build Vimbrowser, CEF, DevTools, or retained tests.
- [ ] **T05:** Keep focused surviving upstream unit/browser tests for PDF, media,
      WebRTC, storage, networking, and other retained critical behavior.
- [ ] **T06:** Build, measure, commit, and push each large test-corpus deletion.

## G. GN graph and build-system simplification

- [ ] **G01:** Replace CEF's dependency on monolithic `//chrome:dependencies` and
      `//chrome/browser:browser` with an explicit vimbrowser/CEF production
      aggregation.
- [ ] **G02:** Split surviving Chrome browser code into minimal targets so removed
      feature families cannot reenter through circular monolithic dependencies.
- [ ] **G03:** Delete dead GN args, buildflags, conditionals, source lists, config
      templates, generated headers, and resource defines for removed features.
- [ ] **G04:** Delete obsolete GRIT resources, WebUI bundles, TypeScript/JavaScript,
      images, strings, locale entries, and generated pack inputs.
- [ ] **G05:** Audit third-party dependencies in the final production closure and
      physically remove libraries used only by deleted features/platforms/tests.
- [ ] **G06:** Audit build-time Python, Rust, Node, Java, and generator tooling;
      retain only tools required for clean Linux/macOS production builds and
      retained tests.
- [ ] **G07:** Reduce supported toolchains to Linux x86-64, macOS retained
      architectures, and architecture-neutral Linux support needed for possible
      future ARM.
- [ ] **G08:** Ensure a clean checkout can generate and build without fetching
      deleted CIPD packages or referencing deleted source paths.
- [ ] **G09:** Measure target count, Ninja/Siso input count, manifest size, GN
      generation time, clean build time, and incremental build time before and
      after graph replacement.
- [ ] **G10:** Build, install, smoke test, measure, commit, and push graph
      simplification in reviewable increments.

## F. Per-file and per-target sweep

Begin only after the approved broad feature families have been removed.

- [ ] **F01:** Generate the complete `//cef:libcef` production source/input and
      runtime dependency manifests for Linux and macOS.
- [ ] **F02:** Classify every surviving first-party directory as production,
      build-time-only, retained-test-only, generated, or unused.
- [ ] **F03:** Delete every tracked source/resource file that is neither in a
      retained production/build closure nor an approved test closure.
- [ ] **F04:** Review each surviving GN target for unnecessary sources, public
      dependencies, data dependencies, circular includes, and resource bundles.
- [ ] **F05:** Review each surviving C/C++ file for dead platform branches, dead
      feature branches, unused classes, adapters for deleted products, and
      unnecessary includes.
- [ ] **F06:** Collapse compatibility skeletons once their final callers become
      simple enough to remove them entirely.
- [ ] **F07:** Remove stale comments, TODOs, metrics enums, histograms, feature
      flags, preferences, policy keys, and generated schema entries associated
      with deleted functionality.
- [ ] **F08:** Run include/dependency checks and compile after small batches; do
      not accumulate a large unbuildable per-file diff.
- [ ] **F09:** Commit and push bisectable batches grouped by target or directory.

## M. Measurement and regression infrastructure

- [ ] **M01:** Keep a machine-readable benchmark ledger per cleanup commit/tag:
      tracked files, tracked bytes, checkout bytes, source files, source LOC,
      `.git` bytes, out-directory bytes, intermediate bytes, runtime bytes,
      `libcef.so` bytes, shell bytes, and package/install bytes.
- [ ] **M02:** Record GN generation time and target count.
- [ ] **M03:** Record clean build wall time, CPU time, peak memory, disk writes,
      and resulting intermediates on comparable hardware/settings.
- [ ] **M04:** Record no-op and representative incremental build times.
- [ ] **M05:** Record startup IPC-ready time, first-page-complete time, tab switch,
      screenshot, restored-session startup, and benchmark variance.
- [ ] **M06:** Add repeatable runtime memory measurements for browser, renderer,
      GPU, network, and utility processes under fixed one-tab and multi-tab
      scenarios.
- [ ] **M07:** Maintain a retained-feature integration suite covering navigation,
      history, bookmarks, tabs/windows, downloads, uploads, cookies, localStorage,
      IndexedDB, service workers, media, fullscreen, PDF, DevTools,
      camera/microphone/WebRTC, codecs/DRM, and retained PWA behavior.
- [ ] **M08:** Add explicit negative tests for removed features so stubs do not
      silently regress into prompts, crashes, or partially functioning UI.
- [ ] **M09:** Define acceptable regression thresholds before optimization-only
      changes: functionality is mandatory; performance/size changes must be
      measured rather than assumed.
- [ ] **M10:** Run a clean build and full benchmark at major broad-sweep
      checkpoints, not after every one-line compile repair.

## C. Installed runtime and packaging cleanup

- [ ] **C01:** Inventory every file copied into the Linux and macOS runtime.
- [ ] **C02:** Remove helper executables, packs, locales, snapshots, libraries,
      resources, and manifests used only by deleted features/platforms.
- [ ] **C03:** Decide the exact retained locale set; do not delete user-required
      locales by assumption.
- [ ] **C04:** Preserve sandboxing and required runtime security data unless a
      replacement is deliberately implemented and tested.
- [ ] **C05:** Verify codecs, Widevine discovery, PDF assets, DevTools resources,
      file chooser, and media libraries after runtime slimming.
- [ ] **C06:** Make `make install` reproducibly install only the minimal validated
      runtime.
- [ ] **C07:** Measure installed bytes and startup after each runtime batch.

## V. Final platform validation

- [ ] **V01:** Complete a clean Linux x86-64 GN generation and production build
      from a fresh output directory.
- [ ] **V02:** Complete the full Linux retained-feature and negative-feature
      integration suites.
- [ ] **V03:** Complete a clean macOS production build and retained-feature smoke
      suite on real macOS hardware.
- [ ] **V04:** Verify no Wayland, Windows, iOS, Android, ChromeOS, Fuchsia, Cast,
      or official Chrome packaging target remains in the production closure.
- [ ] **V05:** Verify Linux architecture conditionals remain suitable for a future
      X11 ARM/ARM64 port; optionally perform GN generation or a real build when
      hardware/toolchain access exists.
- [ ] **V06:** Compare final clean/incremental build, checkout, output, install,
      startup, and memory measurements against `pre-cleanup`.

## Q. Repository and history cleanup

Perform history-rewriting operations only after explicit user review, a backup,
and confirmation that the final broad/per-file state is safely pushed.

- [ ] **Q01:** Verify only `main` is retained locally and remotely; remove stale
      branches after explicit confirmation.
- [ ] **Q02:** Audit tags and keep intentional recovery/benchmark anchors such as
      `pre-cleanup`; remove accidental or obsolete tags after confirmation.
- [ ] **Q03:** Identify the exact first vimbrowser fork/customization commit that
      should become the rewritten history root.
- [ ] **Q04:** Back up the repository and record all commit/tag IDs before history
      rewriting.
- [ ] **Q05:** Rewrite history to discard unwanted pre-fork Chromium history and
      remove deleted giant blobs from reachable history without losing the
      vimbrowser development history the user wants to keep.
- [ ] **Q06:** Coordinate and explicitly approve any force-push needed by the
      rewritten `main`; never do this implicitly as part of a source cleanup.
- [ ] **Q07:** Expire obsolete reflogs and run an aggressive repository repack/GC
      after the rewritten remote and backup are verified.
- [ ] **Q08:** Measure final `.git` size and total checkout size.
- [ ] **Q09:** Remove obsolete build outputs, distributions, caches, and abandoned
      benchmark directories that are artifacts rather than source.

## Definition of done

- [ ] Every explicitly approved feature family is physically removed or reduced
      to a documented, demonstrably necessary compatibility skeleton.
- [ ] Every retained feature in the product contract has automated smoke or
      integration coverage.
- [ ] Linux X11 and macOS clean builds pass from a fresh checkout/output.
- [ ] `make` and `make install` pass at the final revision.
- [ ] Relevant surviving unit/browser tests pass.
- [ ] Negative tests prove removed functionality stays absent and does not
      prompt/crash.
- [ ] Final source, checkout, Git, build, output, install, clean/incremental time,
      startup, and memory measurements are recorded against `pre-cleanup`.
- [ ] Final `main` is pushed, recovery anchors are documented, and repository
      history cleanup has been separately reviewed and completed.
