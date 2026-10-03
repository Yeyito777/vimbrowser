// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTO_NSS_UTIL_H_
#define CRYPTO_NSS_UTIL_H_

#include <stdint.h>

#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "crypto/crypto_export.h"

namespace base {
class Time;
}  // namespace base

// This file specifically doesn't depend on any NSS or NSPR headers because it
// is included by various (non-crypto) parts of chrome to call the
// initialization functions.
namespace crypto {

class ScopedAllowBlockingForNSS : public base::ScopedAllowBlocking {};

// Initialize NRPR if it isn't already initialized.  This function is
// thread-safe, and NSPR will only ever be initialized once.
CRYPTO_EXPORT void EnsureNSPRInit();

// Initialize NSS if it isn't already initialized.  This must be called before
// any other NSS functions.  This function is thread-safe, and NSS will only
// ever be initialized once.
CRYPTO_EXPORT void EnsureNSSInit();

// Check if the current NSS version is greater than or equals to |version|.
// A sample version string is "3.12.3".
bool CheckNSSVersion(const char* version);


// Convert a NSS PRTime value into a base::Time object.
// We use a int64_t instead of PRTime here to avoid depending on NSPR headers.
CRYPTO_EXPORT base::Time PRTimeToBaseTime(int64_t prtime);

// Convert a base::Time object into a PRTime value.
// We use a int64_t instead of PRTime here to avoid depending on NSPR headers.
CRYPTO_EXPORT int64_t BaseTimeToPRTime(base::Time time);

CRYPTO_EXPORT base::FilePath GetDefaultNSSConfigDirectory();

}  // namespace crypto

#endif  // CRYPTO_NSS_UTIL_H_
