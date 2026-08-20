// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/glic/widget/glic_window_event_observer.h"

#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/glic/widget/glic_view.h"
#include "chrome/browser/glic/widget/glic_widget.h"
#include "chrome/browser/glic/widget/glic_window_animator.h"
#include "ui/events/event_observer.h"
#include "ui/views/event_monitor.h"
#include "ui/views/widget/widget.h"


namespace glic {

// Helper class for observing mouse and key events from native window.
class GlicWindowEventObserver::WindowEventObserverImpl
    : public ui::EventObserver {
 public:
  WindowEventObserverImpl(GlicWindowEventObserver* observer, GlicView* view)
      : observer_(observer->GetWeakPtr()), view_(view->GetWeakPtr()) {
    event_monitor_ = views::EventMonitor::CreateWindowMonitor(
        this, view->GetWidget()->GetNativeWindow(),
        {
            ui::EventType::kMousePressed,
            ui::EventType::kMouseReleased,
            ui::EventType::kMouseDragged,
            ui::EventType::kTouchReleased,
            ui::EventType::kTouchPressed,
            ui::EventType::kTouchMoved,
            ui::EventType::kTouchCancelled,
        });
  }

  ~WindowEventObserverImpl() override = default;

  // Determines if the mouse has moved beyond a certain distance to
  // start a drag.
  bool ShouldStartDrag(const gfx::Point& current_mouse_location) {
    // Determine if the mouse has moved beyond a minimum elasticity distance
    // in any direction from the starting point.
    static const int kMinimumDragDistance = 10;
    int x_offset =
        abs(current_mouse_location.x() - initial_click_location_.x());
    int y_offset =
        abs(current_mouse_location.y() - initial_click_location_.y());
    return sqrt(pow(static_cast<float>(x_offset), 2) +
                pow(static_cast<float>(y_offset), 2)) > kMinimumDragDistance;
  }

  void OnEvent(const ui::Event& event) override {
    if (!view_ || !observer_) {
      return;
    }

    gfx::Point mouse_location = event_monitor_->GetLastMouseLocation();
    views::View::ConvertPointFromScreen(view_.get(), &mouse_location);
    if (event.type() == ui::EventType::kMousePressed) {
      mouse_down_in_draggable_area_ =
          view_->IsPointWithinDraggableRegion(mouse_location);
      initial_click_location_ = mouse_location;
    }
    if (event.type() == ui::EventType::kMouseReleased ||
        event.type() == ui::EventType::kMouseExited) {
      mouse_down_in_draggable_area_ = false;
      initial_click_location_ = gfx::Point();
    }

    // Window should only be dragged if a corresponding mouse drag event was
    // initiated in the draggable area.
    if (mouse_down_in_draggable_area_ &&
        event.type() == ui::EventType::kMouseDragged &&
        ShouldStartDrag(mouse_location)) {
      observer_->HandleWindowDragWithOffset(
          initial_click_location_.OffsetFromOrigin());
    }
  }

 private:
  base::WeakPtr<GlicWindowEventObserver> observer_;
  base::WeakPtr<GlicView> view_;
  std::unique_ptr<views::EventMonitor> event_monitor_;

  // Tracks whether the mouse is pressed and was initially within a draggable
  // area of the window.
  bool mouse_down_in_draggable_area_ = false;


  // Tracks the initial kMousePressed location of a potential drag.
  gfx::Point initial_click_location_;
};

GlicWindowEventObserver::GlicWindowEventObserver(
    base::WeakPtr<GlicWidget> glic_widget,
    Delegate* delegate)
    : widget_(glic_widget), delegate_(delegate) {}

GlicWindowEventObserver::~GlicWindowEventObserver() = default;

void GlicWindowEventObserver::SetDraggingAreasAndWatchForMouseEvents() {
  if (window_event_observer_impl_) {
    return;
  }

  GlicView* glic_view = widget_->GetGlicView();
  if (!glic_view) {
    return;
  }

  window_event_observer_impl_ =
      std::make_unique<WindowEventObserverImpl>(this, glic_view);
}

void GlicWindowEventObserver::HandleWindowDragWithOffset(
    const gfx::Vector2d& mouse_offset) {
  if (in_move_loop_ || !widget_) {
    return;
  }
  in_move_loop_ = true;
  widget_->SetIsDragging(true);
  delegate_->window_animator()->CancelAnimation();
#if BUILDFLAG(IS_MAC)
  widget_->SetCapture(nullptr);
#endif

  // PostTaskAndReply is used to ensure that anything running on the stack
  // (like OnEvent) is finished before the nested run loop in RunMoveLoop
  // starts. It also ensures that the code in OnMoveLoopFinished doesn't run if
  // this is destroyed while RunMoveLoop is running.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTaskAndReply(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<GlicWidget> widget, const gfx::Vector2d& offset) {
            if (widget) {
              widget->RunMoveLoop(
                  offset, views::Widget::MoveLoopSource::kMouse,
                  views::Widget::MoveLoopEscapeBehavior::kDontHide);
            }
          },
          widget_, mouse_offset),
      base::BindOnce(&GlicWindowEventObserver::OnMoveLoopFinished,
                     weak_ptr_factory_.GetWeakPtr()));
}

void GlicWindowEventObserver::OnMoveLoopFinished() {
  in_move_loop_ = false;

  if (widget_) {
    widget_->SetIsDragging(false);
  }

  // The delegate owns this object, so it is guaranteed to be alive if we are.
  delegate_->window_animator()->MaybeAnimateToTargetSize();
  AdjustPositionIfNeeded();
  delegate_->OnDragComplete();
}

void GlicWindowEventObserver::AdjustPositionIfNeeded() {
  if (!widget_) {
    return;
  }
  // Always have at least `kMinimumVisible` px visible from glic window in
  // both vertical and horizontal directions.
  constexpr int kMinimumVisible = 40;
  const auto widget_size = widget_->GetSize();
  const int horizontal_buffer = widget_size.width() - kMinimumVisible;
  const int vertical_buffer = widget_size.height() - kMinimumVisible;

  // Adjust bounds of visible area screen to allow part of glic to go off
  // screen.
  auto workarea = widget_->GetWorkAreaBoundsInScreen();
  workarea.Outset(gfx::Outsets::VH(vertical_buffer, horizontal_buffer));

  auto rect = widget_->GetRestoredBounds();
  rect.AdjustToFit(workarea);
  widget_->SetBounds(rect);
}

}  // namespace glic
