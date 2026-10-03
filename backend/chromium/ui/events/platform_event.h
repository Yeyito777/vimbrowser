// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_EVENTS_PLATFORM_EVENT_H_
#define UI_EVENTS_PLATFORM_EVENT_H_

#include "build/build_config.h"

// TODO(crbug.com/40267204): Both gfx::NativeEvent and ui::PlatformEvent
// are typedefs for native event types on different platforms, but they're
// slightly different and used in different places. They should be merged.

#if BUILDFLAG(IS_APPLE)
#include "base/apple/owned_objc.h"
#endif

namespace ui {
class Event;
}

namespace ui {

// Cross platform typedefs for native event types.
#if BUILDFLAG(IS_OZONE)
using PlatformEvent = ui::Event*;
#elif BUILDFLAG(IS_MAC)
using PlatformEvent = base::apple::OwnedNSEvent;
#else
using PlatformEvent = void*;
#endif

}  // namespace ui

#endif  // UI_EVENTS_PLATFORM_EVENT_H_
