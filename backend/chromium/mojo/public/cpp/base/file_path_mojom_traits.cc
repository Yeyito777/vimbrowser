// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/351564777): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "mojo/public/cpp/base/file_path_mojom_traits.h"

#include "build/build_config.h"

namespace mojo {

// static
bool StructTraits<mojo_base::mojom::FilePathDataView, base::FilePath>::Read(
    mojo_base::mojom::FilePathDataView data,
    base::FilePath* out) {
  base::FilePath::StringViewType path_view;
  if (!data.ReadPath(&path_view)) {
    return false;
  }
  *out = base::FilePath(path_view);
  return true;
}

// static
// static
const base::FilePath::StringType&
StructTraits<mojo_base::mojom::RelativeFilePathDataView, base::FilePath>::path(
    const base::FilePath& path) {
  CHECK(!path.IsAbsolute());
  CHECK(!path.ReferencesParent());
  return path.value();
}

// static
bool StructTraits<mojo_base::mojom::RelativeFilePathDataView, base::FilePath>::
    Read(mojo_base::mojom::RelativeFilePathDataView data, base::FilePath* out) {
  base::FilePath::StringViewType path_view;
  if (!data.ReadPath(&path_view)) {
    return false;
  }
  *out = base::FilePath(path_view);

  if (out->IsAbsolute()) {
    return false;
  }
  if (out->ReferencesParent()) {
    return false;
  }
  return true;
}

}  // namespace mojo
