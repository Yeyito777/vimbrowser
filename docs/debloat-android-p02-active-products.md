# P02 Active Android Product Removal

This checkpoint follows `eb1e110700` and removes Android product code that the
P01 inventory found in or adjacent to the Linux `//cef:libcef` dependency
graph.  Unlike the first deletion batch, these paths could not simply be
removed: shared Chrome, media, Blink, and DevTools targets first had to stop
declaring or using their interfaces.

## Removed products

- `chrome/browser/android` (788 files) is gone.  The desktop sync target no
  longer depends unconditionally on its WebAPK database proto, and the obsolete
  Android public dependency was removed from the browser aggregate.
- `media/base/android` (73 files) is gone.  The cross-platform media roll-up,
  CDM/audio/format visibility lists, tests, Blink tests, and Android-only child
  branches no longer refer to its target or Java/JNI generators.  Desktop
  audio/video, proprietary codecs, and Widevine library-CDM support remain.
- Blink's two-file Android font-lookup mojom and its Android-only Content host
  binding are gone.  The ordinary Linux/macOS font-service paths remain.
- The 37-file desktop Android-device DevTools stack is gone: ADB, USB transport
  and RSA, device discovery, port forwarding, remote browser/page adapters, and
  cast-device plumbing built on the same Android manager.

Local page DevTools and Chrome's normal remote-debugging server/protocol remain
supported.  The protocol method for discovering arbitrary remote locations now
returns a clear unsupported error, and frontend-facing remote-device hooks are
small empty compatibility boundaries instead of retaining the Android manager.

The Google Cloud wrapper also now retries SCP while a newly started VM's SSH
service is coming up.  `inherit_errexit` prevents a failed snapshot sync inside
command substitution from being mistaken for a successful build of a stale
worker tree.

## Validation

Worker tree `7a9b8ffa0412a37af0bac791d80d9c39462c9e7a` passed:

- Linux GN generation (27,317 targets, down from 27,322 at the previous
  checkpoint);
- the incremental Chromium/CEF build;
- a clean 205-step CMake/Ninja vimbrowser shell build;
- CTest 1/1;
- SHA-256 verification of the archive and every fetched runtime file;
- the full local benchmark with `CHECK: PASS`; and
- stable-runtime checksum verification before and after all operations.

After documentation and include cleanup, exact staged tree
`60ebba5d52e11ba8aba8594e595945adaafb4d41` repeated GN generation, the
Chromium/CEF and clean shell builds, CTest, checksummed fetch, and stable-runtime
verification successfully.  The only later edit records that final tree here.

This change does not run normal `make install` and does not replace or restart
the user's installed/running browser.  The worker was stopped after validation.

## Remaining P02 exceptions

This is still not P02 completion.  Next are the Android-only entries embedded
in shared ANGLE/GL/media/generated source lists, then the Perfetto/Catapult
schemas, importers, metrics, and trace resources recorded by P01.  Dead Android
GN/resource/manifest branches in retained cross-platform files remain a
separate explicit cleanup pass; globally parsed SDK/toolchain roots remain
deferred to P04.
