// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/base/read_only_file_mojom_traits.h"

#include "base/files/file.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif  // BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)


namespace mojo {
namespace {

// True if the underlying handle is only readable. Where possible this excludes
// deletion, writing, truncation, append and other operations that might modify
// the underlying file. False if we can tell that the file could be modified.
// On platforms where we cannot test the handle, always returns true.
bool IsReadOnlyFile(base::File& file) {
  bool is_readonly = true;
#if BUILDFLAG(IS_FUCHSIA) || (BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_AIX))
  is_readonly =
      (fcntl(file.GetPlatformFile(), F_GETFL) & O_ACCMODE) == O_RDONLY;
#endif
  return is_readonly;
}

bool IsPhysicalFile(base::File& file) {
  // This may block but in practice this is unlikely for already opened
  // physical files.
  struct stat st;
  if (fstat(file.GetPlatformFile(), &st) != 0) {
    return false;
  }
  return S_ISREG(st.st_mode);
}

}  // namespace

mojo::PlatformHandle StructTraits<mojo_base::mojom::ReadOnlyFileDataView,
                                  base::File>::fd(base::File& file) {
  CHECK(file.IsValid());
  // For now we require real files as on some platforms it is too difficult to
  // be sure that more general handles cannot be written or made writable. This
  // could be relaxed if an interface needs readonly pipes. This check may block
  // so cannot be enabled in release builds.
  DCHECK(IsPhysicalFile(file));
  CHECK(IsReadOnlyFile(file));

  return mojo::PlatformHandle(
      base::ScopedPlatformFile(file.TakePlatformFile()));
}

bool StructTraits<mojo_base::mojom::ReadOnlyFileDataView, base::File>::Read(
    mojo_base::mojom::ReadOnlyFileDataView data,
    base::File* file) {
  *file = base::File(data.TakeFd().TakePlatformFile(), data.async());
  return true;
}

}  // namespace mojo
