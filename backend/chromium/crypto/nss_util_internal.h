// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTO_NSS_UTIL_INTERNAL_H_
#define CRYPTO_NSS_UTIL_INTERNAL_H_

#include <secmodt.h>

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "crypto/crypto_export.h"
#include "crypto/scoped_nss_types.h"

namespace base {
class FilePath;
}

// These functions return a type defined in an NSS header, and so cannot be
// declared in nss_util.h.  Hence, they are declared here.

namespace crypto {

// Opens an NSS software database in folder `path`, with the (potentially)
// user-visible description `description`. Returns the slot for the opened
// database, or nullptr if the database could not be opened. Can be called
// multiple times for the same `path`, thread-safe.
CRYPTO_EXPORT ScopedPK11Slot OpenSoftwareNSSDB(const base::FilePath& path,
                                               const std::string& description);

// Closes the underlying database for the `slot`. All remaining slots
// referencing the same database will remain valid objects, but won't be able to
// successfully retrieve certificates, etc. Should be used for all databases
// that were opened with `OpenSoftwareNSSDB` (instead of `SECMOD_CloseUserDB`).
// Can be called multiple times. Returns `SECSuccess` if the database was
// successfully closed, returns `SECFailure` if it was never opened, was already
// closed by an earlier call, or failed to close. Thread-safe.
CRYPTO_EXPORT SECStatus CloseSoftwareNSSDB(PK11SlotInfo* slot);

// A helper class that acquires the SECMOD list read lock while the
// AutoSECMODListReadLock is in scope.
class CRYPTO_EXPORT AutoSECMODListReadLock {
 public:
  AutoSECMODListReadLock();

  AutoSECMODListReadLock(const AutoSECMODListReadLock&) = delete;
  AutoSECMODListReadLock& operator=(const AutoSECMODListReadLock&) = delete;

  ~AutoSECMODListReadLock();

 private:
  raw_ptr<SECMODListLock> lock_;
};


// Loads the given module for this NSS session.
SECMODModule* LoadNSSModule(const char* name,
                            const char* library_path,
                            const char* params);

// Returns the current NSS error message.
std::string GetNSSErrorMessage();

}  // namespace crypto

#endif  // CRYPTO_NSS_UTIL_INTERNAL_H_
