// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/window/native_frame_view.h"

#include "build/build_config.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/widget/native_widget.h"
#include "ui/views/widget/widget.h"


namespace views {

////////////////////////////////////////////////////////////////////////////////
// NativeFrameView, public:

NativeFrameView::NativeFrameView(Widget* widget) : widget_(widget) {}

NativeFrameView::~NativeFrameView() = default;

////////////////////////////////////////////////////////////////////////////////
// NativeFrameView, FrameView overrides:

gfx::Rect NativeFrameView::GetBoundsForClientView() const {
  return gfx::Rect(0, 0, width(), height());
}

gfx::Rect NativeFrameView::GetWindowBoundsForClientBounds(
    const gfx::Rect& client_bounds) const {
  // Enforce minimum size (1, 1) in case that |client_bounds| is passed with
  // empty size.
  gfx::Rect window_bounds = client_bounds;
  if (window_bounds.IsEmpty()) {
    window_bounds.set_size(gfx::Size(1, 1));
  }
  return window_bounds;
}

int NativeFrameView::NonClientHitTest(const gfx::Point& point) {
  return widget_->client_view()->NonClientHitTest(point);
}

gfx::Size NativeFrameView::CalculatePreferredSize(
    const SizeBounds& available_size) const {
  gfx::Size client_preferred_size =
      widget_->client_view()->GetPreferredSize(available_size);
  return widget_->non_client_view()
      ->GetWindowBoundsForClientBounds(gfx::Rect(client_preferred_size))
      .size();
}

gfx::Size NativeFrameView::GetMinimumSize() const {
  return widget_->client_view()->GetMinimumSize();
}

gfx::Size NativeFrameView::GetMaximumSize() const {
  return widget_->client_view()->GetMaximumSize();
}

BEGIN_METADATA(NativeFrameView)
END_METADATA

}  // namespace views
