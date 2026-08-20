// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/public/common/chrome_debug_urls.h"

#include "base/debug/alias.h"
#include "base/debug/asan_invalid_access.h"
#include "base/debug/dump_without_crashing.h"
#include "base/logging.h"
#include "base/process/process.h"
#include "base/threading/platform_thread.h"
#include "build/build_config.h"
#include "third_party/blink/common/crash_helpers.h"
#include "third_party/blink/common/rust_crash/src/lib.rs.h"
#include "url/gurl.h"

#include <signal.h>


namespace blink {

bool IsRendererDebugURL(const GURL& url) {
  if (!url.is_valid())
    return false;

  if (url.SchemeIs(url::kJavaScriptScheme))
    return true;

  if (!url.SchemeIs("chrome"))
    return false;

  if (url == kChromeUICheckCrashURL || url == kChromeUIBadCastCrashURL ||
      url == kChromeUICrashURL || url == kChromeUIDumpURL ||
      url == kChromeUIKillURL || url == kChromeUIHangURL ||
      url == kChromeUIShorthangURL || url == kChromeUIMemoryExhaustURL ||
      url == kChromeUICrashRustURL) {
    return true;
  }

#if defined(ADDRESS_SANITIZER)
  if (url == kChromeUICrashHeapOverflowURL ||
      url == kChromeUICrashHeapUnderflowURL ||
      url == kChromeUICrashUseAfterFreeURL ||
      url == kChromeUICrashRustOverflowURL) {
    return true;
  }
#endif  // defined(ADDRESS_SANITIZER)


#if DCHECK_IS_ON()
  if (url == kChromeUICrashDcheckURL)
    return true;
#endif

#if BUILDFLAG(IS_WIN) && defined(ADDRESS_SANITIZER)
  if (url == kChromeUICrashCorruptHeapBlockURL ||
      url == kChromeUICrashCorruptHeapURL) {
    return true;
  }
#endif

  return false;
}

namespace {

// The following methods are outside of the anonymous namespace to ensure that
// the corresponding symbols get emitted even on symbol_level 1.
NOINLINE void ExhaustMemory() {
  volatile void* ptr = nullptr;
  do {
    ptr = malloc(0x10000000);
    base::debug::Alias(&ptr);
  } while (ptr);
}

#if defined(ADDRESS_SANITIZER)
NOINLINE void MaybeTriggerAsanError(const GURL& url) {
  // NOTE(rogerm): We intentionally perform an invalid heap access here in
  //     order to trigger an Address Sanitizer (ASAN) error report.
  if (url == kChromeUICrashHeapOverflowURL) {
    LOG(ERROR) << "Intentionally causing ASAN heap overflow"
               << " because user navigated to " << url.spec();
    base::debug::AsanHeapOverflow();
  } else if (url == kChromeUICrashHeapUnderflowURL) {
    LOG(ERROR) << "Intentionally causing ASAN heap underflow"
               << " because user navigated to " << url.spec();
    base::debug::AsanHeapUnderflow();
  } else if (url == kChromeUICrashUseAfterFreeURL) {
    LOG(ERROR) << "Intentionally causing ASAN heap use-after-free"
               << " because user navigated to " << url.spec();
    base::debug::AsanHeapUseAfterFree();
  } else if (url == kChromeUICrashRustOverflowURL) {
    // Ensure that ASAN works even in Rust code.
    LOG(ERROR) << "Intentionally causing ASAN heap overflow in Rust"
               << " because user navigated to " << url.spec();
    crash_in_rust_with_overflow();
  }
}
#endif  // ADDRESS_SANITIZER

}  // namespace

void HandleChromeDebugURL(const GURL& url) {
  DCHECK(IsRendererDebugURL(url) && !url.SchemeIs("javascript"));
  if (url == kChromeUIBadCastCrashURL) {
    LOG(ERROR) << "Intentionally crashing (with bad cast)"
               << " because user navigated to " << url.spec();
    internal::BadCastCrashIntentionally();
  } else if (url == kChromeUICrashURL) {
    LOG(ERROR) << "Intentionally crashing (with null pointer dereference)"
               << " because user navigated to " << url.spec();
    internal::CrashIntentionally();
  } else if (url == kChromeUICrashRustURL) {
    // Cause a typical crash in Rust code, so we can test that call stack
    // collection and symbol mangling work across the language boundary.
    crash_in_rust();
  } else if (url == kChromeUIDumpURL) {
    // This URL will only correctly create a crash dump file if content is
    // hosted in a process that has correctly called
    // base::debug::SetDumpWithoutCrashingFunction.  Refer to the documentation
    // of base::debug::DumpWithoutCrashing for more details.
    base::debug::DumpWithoutCrashing();
  } else if (url == kChromeUIKillURL) {
    LOG(ERROR) << "Intentionally terminating current process because user"
                  " navigated to "
               << url.spec();
    // Simulate termination such that the base::GetTerminationStatus() API will
    // return TERMINATION_STATUS_PROCESS_WAS_KILLED.
    PCHECK(kill(base::Process::Current().Pid(), SIGTERM) == 0);
  } else if (url == kChromeUIHangURL) {
    LOG(ERROR) << "Intentionally hanging ourselves with sleep infinite loop"
               << " because user navigated to " << url.spec();
    for (;;) {
      base::PlatformThread::Sleep(base::Seconds(1));
    }
  } else if (url == kChromeUIShorthangURL) {
    LOG(ERROR) << "Intentionally sleeping renderer for 20 seconds"
               << " because user navigated to " << url.spec();
    base::PlatformThread::Sleep(base::Seconds(20));
  } else if (url == kChromeUIMemoryExhaustURL) {
    LOG(ERROR)
        << "Intentionally exhausting renderer memory because user navigated to "
        << url.spec();
    ExhaustMemory();
  } else if (url == kChromeUICheckCrashURL) {
    LOG(ERROR) << "Intentionally causing CHECK because user navigated to "
               << url.spec();
    CHECK(false);
  }


#if DCHECK_IS_ON()
  if (url == kChromeUICrashDcheckURL) {
    LOG(ERROR) << "Intentionally causing DCHECK because user navigated to "
               << url.spec();

    DCHECK(false) << "Intentional DCHECK.";
  }
#endif

#if defined(ADDRESS_SANITIZER)
  MaybeTriggerAsanError(url);
#endif  // ADDRESS_SANITIZER
}

}  // namespace blink
