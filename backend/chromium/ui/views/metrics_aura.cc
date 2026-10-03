// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/time/time.h"
#include "build/build_config.h"
#include "ui/views/metrics.h"


namespace views {

base::TimeDelta GetDoubleClickInterval() {
  // TODO(jennyz): This value may need to be adjusted on different platforms.
  constexpr base::TimeDelta kDefaultDoubleClickInterval =
      base::Milliseconds(500);
  return kDefaultDoubleClickInterval;
}

base::TimeDelta GetMenuShowDelay() {
  return base::Milliseconds(0);
}

}  // namespace views
