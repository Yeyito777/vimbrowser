// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/test/widget_test_api.h"

#include "base/notimplemented.h"
#include "ui/views/widget/widget.h"

#if BUILDFLAG(IS_LINUX)

#include "base/test/run_until.h"
#include "ui/aura/window_tree_host_platform.h"
#include "ui/platform_window/extensions/x11_extension.h"

#endif  // BUILDFLAG(IS_LINUX)

namespace views {

void DisableActivationChangeHandlingForTests() {
  Widget::SetDisableActivationChangeHandling(
      Widget::DisableActivationChangeHandlingType::kIgnore);
}

AsyncWidgetRequestWaiter::AsyncWidgetRequestWaiter(Widget& widget)
    : widget_(widget) {}

AsyncWidgetRequestWaiter::~AsyncWidgetRequestWaiter() {
  CHECK(waited_)
      << "AsyncWidgetRequestWaiter has no effect unless `Wait` is called.";
}

void AsyncWidgetRequestWaiter::Wait() {
  CHECK(!waited_) << "`Wait` may only be called once.";
#if BUILDFLAG(IS_LINUX)
  auto* host = aura::WindowTreeHostPlatform::GetHostForWindow(
      widget_->GetNativeWindow());
  // Setting the window bounds on X11 is asynchronous, so the platform window
  // pretends the bounds change completed successfully until it can sync with
  // the WM. This may cause inconsistent state with respect to other caches such
  // as x11::WindowCache. Wait for the WM sync to complete to ensure
  // consistency.
  if (auto* x11_extension = ui::GetX11Extension(*host->platform_window())) {
    CHECK(base::test::RunUntil(
        [&]() { return !x11_extension->IsWmSyncActiveForTest(); }));
  } else {
    NOTIMPLEMENTED_LOG_ONCE();
  }
#else  // BUILDFLAG(IS_LINUX)
  NOTIMPLEMENTED_LOG_ONCE();
#endif
  waited_ = true;
}

}  // namespace views
