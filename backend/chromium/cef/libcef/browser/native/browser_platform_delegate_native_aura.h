// Copyright 2020 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CEF_LIBCEF_BROWSER_NATIVE_BROWSER_PLATFORM_DELEGATE_NATIVE_AURA_H_
#define CEF_LIBCEF_BROWSER_NATIVE_BROWSER_PLATFORM_DELEGATE_NATIVE_AURA_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "cef/libcef/browser/native/browser_platform_delegate_native.h"
#include "ui/compositor/compositor_animation_observer.h"
#include "ui/compositor/compositor_observer.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d.h"

namespace content {
class RenderWidgetHost;
class RenderWidgetHostViewAura;
}

namespace ui {
class Compositor;
}

// Windowed browser implementation for Aura platforms.
class CefBrowserPlatformDelegateNativeAura
    : public CefBrowserPlatformDelegateNative,
      public ui::CompositorAnimationObserver,
      public ui::CompositorObserver {
 public:
  CefBrowserPlatformDelegateNativeAura(const CefWindowInfo& window_info,
                                       SkColor background_color);
  ~CefBrowserPlatformDelegateNativeAura() override;

  void InstallRootWindowBoundsCallback();

  // CefBrowserPlatformDelegate methods:
  void WebContentsDestroyed(content::WebContents* web_contents) override;
  void RenderViewReady() override;
  bool HasFpsSample() const override;
  double GetCurrentFps() const override;
  double GetCompositorRefreshRate() const override;
  void SendVimbrowserBrowserCommandKeyEvent(const CefKeyEvent& event) override;
  void SendKeyEvent(const CefKeyEvent& event) override;
  void SendMouseClickEvent(const CefMouseEvent& event,
                           CefBrowserHost::MouseButtonType type,
                           bool mouseUp,
                           int clickCount) override;
  void SendMouseMoveEvent(const CefMouseEvent& event, bool mouseLeave) override;
  void SendMouseWheelEvent(const CefMouseEvent& event,
                           int deltaX,
                           int deltaY) override;
  void SendTouchEvent(const CefTouchEvent& event) override;
  std::unique_ptr<CefMenuRunner> CreateMenuRunner() override;
  gfx::Point GetScreenPoint(const gfx::Point& view,
                            bool want_dip_coords) const override;

  // CefBrowserPlatformDelegateNative methods:
  input::NativeWebKeyboardEvent TranslateWebKeyEvent(
      const CefKeyEvent& key_event) const override;
  blink::WebMouseEvent TranslateWebClickEvent(
      const CefMouseEvent& mouse_event,
      CefBrowserHost::MouseButtonType type,
      bool mouseUp,
      int clickCount) const override;
  blink::WebMouseEvent TranslateWebMoveEvent(const CefMouseEvent& mouse_event,
                                             bool mouseLeave) const override;
  blink::WebMouseWheelEvent TranslateWebWheelEvent(
      const CefMouseEvent& mouse_event,
      int deltaX,
      int deltaY) const override;

  // Translate CEF events to Chromium UI events.
  virtual ui::KeyEvent TranslateUiKeyEvent(
      const CefKeyEvent& key_event) const = 0;
  virtual ui::MouseEvent TranslateUiClickEvent(
      const CefMouseEvent& mouse_event,
      CefBrowserHost::MouseButtonType type,
      bool mouseUp,
      int clickCount) const;
  virtual ui::MouseEvent TranslateUiMoveEvent(const CefMouseEvent& mouse_event,
                                              bool mouseLeave) const;
  virtual ui::MouseWheelEvent TranslateUiWheelEvent(
      const CefMouseEvent& mouse_event,
      int deltaX,
      int deltaY) const;
  virtual gfx::Vector2d GetUiWheelEventOffset(int deltaX, int deltaY) const;

  // Returns the root window bounds in screen DIP coordinates.
  virtual std::optional<gfx::Rect> GetRootWindowBounds() {
    return std::nullopt;
  }

  // ui::CompositorAnimationObserver / ui::CompositorObserver methods:
  void OnAnimationStep(base::TimeTicks timestamp) override;
  void OnCompositingStarted(ui::Compositor* compositor,
                            base::TimeTicks start_time) override;
  void OnCompositingShuttingDown(ui::Compositor* compositor) override;

 protected:
  base::OnceClosure GetWidgetDeleteCallback();

  std::optional<gfx::Rect> RootWindowBoundsCallback();

  static base::TimeTicks GetEventTimeStamp();
  static int TranslateUiEventModifiers(uint32_t cef_modifiers);
  static int TranslateUiChangedButtonFlags(uint32_t cef_modifiers);

  // Widget hosting the web contents. It will be deleted automatically when the
  // associated root window is destroyed.
  raw_ptr<views::Widget> window_widget_ = nullptr;

 private:
  // Will only be called if the Widget is deleted before
  // CefBrowserHostBase::DestroyBrowser() is called.
  void WidgetDeleted();

  content::RenderWidgetHostViewAura* GetHostView() const;
  void InstallFpsObserver();
  void RemoveFpsObserver();
  void RecordFrameSubmission(base::TimeTicks now);
  void StartSmoothScrollAnimation();
  void StopSmoothScrollAnimation();
  void AbortSmoothScroll();
  void ResetSmoothScrollState();
  content::RenderWidgetHost* CurrentSmoothScrollHost() const;
  gfx::PointF SmoothScrollPosition() const;
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
  raw_ptr<ui::Compositor> fps_observed_compositor_ = nullptr;
  int fps_frame_count_ = 0;
  double fps_current_ = 0.0;
  base::TimeTicks fps_sample_start_;
  bool fps_has_sample_ = false;

  base::WeakPtrFactory<CefBrowserPlatformDelegateNativeAura> weak_ptr_factory_{
      this};
};

#endif  // CEF_LIBCEF_BROWSER_NATIVE_BROWSER_PLATFORM_DELEGATE_NATIVE_AURA_H_
