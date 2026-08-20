// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/enterprise/data_controls/core/browser/action_context.h"

namespace data_controls {

bool ActionSource::empty() const {
  // `ActionSource` should represent either:
  // - A browser tab with the `url` field, and possible `incognito` and/or
  //   `other_profile` set to true.
  // - The OS clipboard with `os_clipboard` set to true.
  return url.is_empty() && !os_clipboard;
}

bool ActionDestination::empty() const {
  // `ActionDestination` should represent either:
  // - A browser tab with the `url` field, and possible `incognito` and/or
  //   `other_profile` set to true.
  // - The OS clipboard with `os_clipboard` set to true.
  // - A separate application represented by `component` (CrOS-only).
  return url.is_empty() && !os_clipboard;
}

}  // namespace data_controls
