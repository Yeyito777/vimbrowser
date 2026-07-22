// Copyright 2015 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CEF_LIBCEF_BROWSER_NATIVE_BROWSER_PLATFORM_DELEGATE_NATIVE_H_
#define CEF_LIBCEF_BROWSER_NATIVE_BROWSER_PLATFORM_DELEGATE_NATIVE_H_

#include "base/memory/raw_ptr.h"
#include "cef/libcef/browser/alloy/browser_platform_delegate_alloy.h"
#include "ui/compositor/compositor_animation_observer.h"
#include "ui/compositor/compositor_observer.h"
#include "ui/gfx/geometry/point_f.h"

namespace content {
class RenderWidgetHost;
class RenderWidgetHostViewBase;
}  // namespace content

namespace ui {
class Compositor;
}  // namespace ui

// Base implementation of native browser functionality.
class CefBrowserPlatformDelegateNative : public CefBrowserPlatformDelegateAlloy,
                                         public ui::CompositorAnimationObserver,
                                         public ui::CompositorObserver {
 public:
  // Used by the windowless implementation to override specific functionality
  // when delegating to the native implementation.
  class WindowlessHandler {
   public:
    // Returns the parent window handle.
    virtual CefWindowHandle GetParentWindowHandle() const = 0;

    // Convert from view DIP coordinates to screen coordinates. If
    // |want_dip_coords| is true return DIP instead of device (pixel)
    // coordinates on Windows/Linux.
    virtual gfx::Point GetParentScreenPoint(const gfx::Point& view,
                                            bool want_dip_coords) const = 0;

   protected:
    virtual ~WindowlessHandler() = default;
  };

  ~CefBrowserPlatformDelegateNative() override;

  // CefBrowserPlatformDelegate methods:
  void WebContentsDestroyed(content::WebContents* web_contents) override;
  void RenderViewReady() override;
  SkColor GetBackgroundColor() const override;
  void WasResized() override;
  void NotifyScreenInfoChanged() override;
  bool HasFpsSample() const override;
  double GetCurrentFps() const override;
  double GetCompositorRefreshRate() const override;
  void SendVimbrowserBrowserCommandKeyEvent(const CefKeyEvent& event) override;
  void SendMouseWheelEvent(const CefMouseEvent& event,
                           int deltaX,
                           int deltaY) override;

  // ui::CompositorAnimationObserver / ui::CompositorObserver methods:
  void OnAnimationStep(base::TimeTicks timestamp) override;
  void OnCompositingStarted(ui::Compositor* compositor,
                            base::TimeTicks start_time) override;
  void OnCompositingShuttingDown(ui::Compositor* compositor) override;

  // Translate CEF events to Chromium/Blink Web events.
  virtual input::NativeWebKeyboardEvent TranslateWebKeyEvent(
      const CefKeyEvent& key_event) const = 0;
  virtual blink::WebMouseEvent TranslateWebClickEvent(
      const CefMouseEvent& mouse_event,
      CefBrowserHost::MouseButtonType type,
      bool mouseUp,
      int clickCount) const = 0;
  virtual blink::WebMouseEvent TranslateWebMoveEvent(
      const CefMouseEvent& mouse_event,
      bool mouseLeave) const = 0;
  virtual blink::WebMouseWheelEvent TranslateWebWheelEvent(
      const CefMouseEvent& mouse_event,
      int deltaX,
      int deltaY) const = 0;

  const CefWindowInfo& window_info() const { return window_info_; }

 protected:
  // Delegates that can wrap a native delegate.
  friend class CefBrowserPlatformDelegateBackground;
  friend class CefBrowserPlatformDelegateChrome;
  friend class CefBrowserPlatformDelegateOsr;
  friend class CefBrowserPlatformDelegateViews;

  CefBrowserPlatformDelegateNative(const CefWindowInfo& window_info,
                                   SkColor background_color);

  // Methods used by delegates that can wrap a native delegate.
  void set_windowless_handler(WindowlessHandler* handler) {
    windowless_handler_ = handler;
    set_as_secondary();
  }

  CefWindowInfo window_info_;
  const SkColor background_color_;

  // Not owned by this object.
  raw_ptr<WindowlessHandler> windowless_handler_ = nullptr;

 private:
  content::RenderWidgetHostViewBase* GetHostView() const;
  void InstallFpsObserver();
  void RemoveFpsObserver();
  void ResetFpsSample();
  void RecordFrameSubmission(base::TimeTicks now);
  void StartSmoothScrollAnimation();
  void StopSmoothScrollAnimation();
  void AbortSmoothScroll();
  void ResetSmoothScrollState();
  content::RenderWidgetHost* RootSmoothScrollHost() const;
  content::RenderWidgetHost* FocusedFrameSmoothScrollHost() const;
  content::RenderWidgetHost* CurrentSmoothScrollHost() const;
  gfx::PointF SmoothScrollPosition() const;
  void SendInstantGestureScroll(const CefMouseEvent& event,
                                int content_dx,
                                int content_dy);
  bool SendGestureScrollBegin(float deltaXHint, float deltaYHint);
  bool SendGestureScrollUpdate(int stepX, int stepY);
  bool SendGestureScrollEnd();
  void TickSmoothScroll(base::TimeTicks now);

  CefMouseEvent smooth_scroll_event_ = {};
  raw_ptr<ui::Compositor> smooth_scroll_compositor_ = nullptr;
  raw_ptr<content::RenderWidgetHost> smooth_scroll_host_ = nullptr;
  double smooth_scroll_dx_ = 0.0;
  double smooth_scroll_dy_ = 0.0;
  double smooth_scroll_subpixel_x_ = 0.0;
  double smooth_scroll_subpixel_y_ = 0.0;
  double smooth_scroll_factor_ = 0.3;
  base::TimeTicks smooth_scroll_last_tick_;
  bool smooth_scroll_scrolling_ = false;
  bool smooth_scroll_sent_begin_ = false;
  bool smooth_scroll_from_hint_target_ = false;
  bool smooth_scroll_target_viewport_ = true;
  raw_ptr<ui::Compositor> fps_observed_compositor_ = nullptr;
  int fps_frame_count_ = 0;
  double fps_current_ = 0.0;
  base::TimeTicks fps_sample_start_;
  bool fps_has_sample_ = false;
};

#endif  // CEF_LIBCEF_BROWSER_NATIVE_BROWSER_PLATFORM_DELEGATE_NATIVE_H_
