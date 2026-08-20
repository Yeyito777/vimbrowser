// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_SAD_TAB_TYPES_H_
#define CHROME_BROWSER_UI_SAD_TAB_TYPES_H_

#include "build/build_config.h"

enum SadTabKind {
  SAD_TAB_KIND_CRASHED,  // Tab crashed.
  SAD_TAB_KIND_OOM,    // Tab ran out of memory.
  SAD_TAB_KIND_KILLED  // Tab killed.
};

#endif  // CHROME_BROWSER_UI_SAD_TAB_TYPES_H_
