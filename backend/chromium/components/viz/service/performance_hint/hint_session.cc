// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/service/performance_hint/hint_session.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/system/sys_info.h"
#include "base/time/time.h"
#include "build/build_config.h"


namespace viz {
std::unique_ptr<HintSessionFactory> HintSessionFactory::Create(
    base::flat_set<base::PlatformThreadId> permanent_thread_ids) {
  return nullptr;
}
}  // namespace viz
