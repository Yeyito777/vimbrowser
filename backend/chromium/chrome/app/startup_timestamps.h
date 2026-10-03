// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_APP_STARTUP_TIMESTAMPS_H_
#define CHROME_APP_STARTUP_TIMESTAMPS_H_

#include "base/time/time.h"
#include "build/build_config.h"

struct StartupTimestamps {
  // Time at which chrome_exe was entered (recorded as early as possible in
  // main()).
  base::TimeTicks exe_entry_point_ticks;
};

#endif  // CHROME_APP_STARTUP_TIMESTAMPS_H_
