// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/chrome_web_dialog_view.h"

#include <memory>

#include "build/build_config.h"
#include "chrome/browser/ui/webui/chrome_web_contents_handler.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/views/controls/webview/web_dialog_view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"


namespace chrome {
namespace {

gfx::NativeWindow CreateWebDialogWidget(views::Widget::InitParams params,
                                        views::WebDialogView* view,
                                        bool show = true) {
  views::Widget* widget = new views::Widget;
  widget->Init(std::move(params));

  if (show) {
    widget->Show();
  }
  return widget->GetNativeWindow();
}

}  // namespace

// Declared in browser_dialogs.h so that others don't need to depend on our .h.
gfx::NativeWindow ShowWebDialog(gfx::NativeView parent,
                                content::BrowserContext* context,
                                ui::WebDialogDelegate* delegate,
                                bool show) {
  return ShowWebDialogWithParams(parent, context, delegate, std::nullopt, show);
}

gfx::NativeWindow ShowWebDialogWithParams(
    gfx::NativeView parent,
    content::BrowserContext* context,
    ui::WebDialogDelegate* delegate,
    std::optional<views::Widget::InitParams> extra_params,
    bool show) {
  views::WebDialogView* view = nullptr;
  view = new views::WebDialogView(context, delegate,
                                  std::make_unique<ChromeWebContentsHandler>());

  // If the corner radius is specified, set it to |views::DialogDelegate|.
  if (extra_params && extra_params->rounded_corners) {
    const auto& radii = extra_params->rounded_corners;
    CHECK_EQ(radii->upper_left(), radii->upper_right());
    CHECK_EQ(radii->upper_left(), radii->lower_left());
    CHECK_EQ(radii->lower_left(), radii->lower_right());
    view->set_corner_radius(radii->upper_left());
  }

  views::Widget::InitParams params(
      views::Widget::InitParams::NATIVE_WIDGET_OWNS_WIDGET);
  if (extra_params) {
    params = std::move(*extra_params);
  }
  params.delegate = view;
  params.parent = parent;
  gfx::NativeWindow window =
      CreateWebDialogWidget(std::move(params), view, show);
  return window;
}

}  // namespace chrome
