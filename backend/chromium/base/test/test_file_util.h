// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_TEST_FILE_UTIL_H_
#define BASE_TEST_TEST_FILE_UTIL_H_

// File utility functions used only by tests.

#include <stddef.h>

#include <memory>

#include "base/files/file_path.h"
#include "base/strings/cstring_view.h"
#include "build/build_config.h"



namespace base {

// Clear a specific file from the system cache like EvictFileFromSystemCache,
// but on failure it will sleep and retry. On the Windows buildbots, eviction
// can fail if the file is marked in use, and this will throw off timings that
// rely on uncached files.
bool EvictFileFromSystemCacheWithRetry(const FilePath& file);

// Wrapper over base::Delete. On Windows repeatedly invokes Delete in case
// of failure to workaround Windows file locking semantics. Returns true on
// success.
bool DieFileDie(const FilePath& file, bool recurse);

// Convenience wrapper for `base::GetTempDir()` that returns the temp dir as a
// `base::FilePath`.
FilePath GetTempDirForTesting();


// Creates a a new unique directory and returns the generated path. The
// directory will be automatically deleted when the test completes. Failure
// upon creation or deletion will cause a test failure.
FilePath CreateUniqueTempDirectoryScopedToTest();

// Creates a new unique temporary directory in `dir` and returns the generated
// path. The directory will be automatically deleted when the test completes.
// Failure upon creation or deletion will cause a test failure.
FilePath CreateUniqueTempDirectoryScopedToTestInDir(const base::FilePath& dir);

// Synchronize all the dirty pages from the page cache to disk (on POSIX
// systems). The Windows analogy for this operation is to 'Flush file buffers'.
// Note: This is currently implemented as a no-op on Windows.
void SyncPageCacheToDisk();

// Clear a specific file from the system cache. After this call, trying
// to access this file will result in a cold load from the hard drive.
bool EvictFileFromSystemCache(const FilePath& file);


// For testing, make the file unreadable or unwritable.
// In POSIX, this does not apply to the root user.
[[nodiscard]] bool MakeFileUnreadable(const FilePath& path);
[[nodiscard]] bool MakeFileUnwritable(const FilePath& path);

// Saves the current permissions for a path, and restores it on destruction.
class FilePermissionRestorer {
 public:
  explicit FilePermissionRestorer(const FilePath& path);

  FilePermissionRestorer(const FilePermissionRestorer&) = delete;
  FilePermissionRestorer& operator=(const FilePermissionRestorer&) = delete;

  ~FilePermissionRestorer();

 private:
  // Forward definition for a structure to hold the file permissions. Will
  // be defined separately in the Windows and POSIX source code.
  struct SavedFilePermissions;

  const FilePath path_;
  std::unique_ptr<SavedFilePermissions> permissions_;
};


}  // namespace base

#endif  // BASE_TEST_TEST_FILE_UTIL_H_
