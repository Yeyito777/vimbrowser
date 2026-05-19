// Copyright 2020 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/browser/native/browser_platform_delegate_native_aura.h"

#include <cmath>

#include "base/notimplemented.h"
#include "cef/libcef/browser/browser_host_base.h"
#include "cef/libcef/browser/native/menu_runner_views_aura.h"
#include "cef/libcef/browser/views/view_util.h"
#include "cef/libcef/common/api_version_util.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_view_aura.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/input/web_gesture_event.h"
#include "ui/compositor/compositor.h"
#include "ui/events/blink/blink_event_util.h"
#include "ui/events/blink/web_input_event.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/views/widget/widget.h"

namespace {
constexpr double kSmoothScrollFactor = 0.3;
constexpr int kVimbrowserBrowserCommandWebModifier = 1 << 27;
constexpr int kVimbrowserSmoothScrollWebModifier = 1 << 28;
constexpr int kVimbrowserHintNewTabWebModifier = 1 << 29;
// Private CEF-side bit set by vimbrowser shell before event translation. It is
// intentionally outside CEF's public modifier range and is not forwarded as a
// Chromium ui::Event flag.
constexpr uint32_t kVimbrowserScrollTargetElementCefModifier = 1u << 30;

bool IsCtrlSpaceBrowserCommand(const CefKeyEvent& event) {
  return (event.modifiers & EVENTFLAG_CONTROL_DOWN) &&
         !(event.modifiers & EVENTFLAG_SHIFT_DOWN) &&
         !(event.modifiers & EVENTFLAG_ALT_DOWN) &&
         !(event.modifiers & EVENTFLAG_COMMAND_DOWN) &&
         (event.windows_key_code == 0x20 || event.character == 0x20 ||
          event.unmodified_character == 0x20);
}
}  // namespace

CefBrowserPlatformDelegateNativeAura::CefBrowserPlatformDelegateNativeAura(
    const CefWindowInfo& window_info,
    SkColor background_color)
    : CefBrowserPlatformDelegateNative(window_info, background_color) {}

CefBrowserPlatformDelegateNativeAura::~CefBrowserPlatformDelegateNativeAura() {
  AbortSmoothScroll();
  RemoveFpsObserver();
}

void CefBrowserPlatformDelegateNativeAura::WebContentsDestroyed(
    content::WebContents* web_contents) {
  AbortSmoothScroll();
  RemoveFpsObserver();
  CefBrowserPlatformDelegateNative::WebContentsDestroyed(web_contents);
}

void CefBrowserPlatformDelegateNativeAura::InstallRootWindowBoundsCallback() {
  auto* host_view = GetHostView();
  CHECK(host_view);

  host_view->SetRootWindowBoundsCallback(base::BindRepeating(
      [](base::WeakPtr<CefBrowserPlatformDelegateNativeAura> self) {
        return self->RootWindowBoundsCallback();
      },
      weak_ptr_factory_.GetWeakPtr()));
}

std::optional<gfx::Rect>
CefBrowserPlatformDelegateNativeAura::RootWindowBoundsCallback() {
  if (CEF_API_IS_ADDED(13700) && browser_) {
    if (auto client = browser_->client()) {
      if (auto handler = client->GetDisplayHandler()) {
        CefRect rect;
        if (handler->GetRootWindowScreenRect(browser_.get(), rect) &&
            !rect.IsEmpty()) {
          return gfx::Rect(rect.x, rect.y, rect.width, rect.height);
        }
      }
    }
  }

  // Call the default platform implementation, if any.
  return GetRootWindowBounds();
}

void CefBrowserPlatformDelegateNativeAura::RenderViewReady() {
  // Navigations/reloads can replace or reset the RenderWidgetHost input router
  // while vimbrowser's synthetic smooth-scroll animation is still running. The
  // old router may have seen our GestureScrollBegin, but the new one has not;
  // sending a delayed GestureScrollEnd into the new router DCHECK-crashes. Treat
  // render-view replacement as cancellation, just like physical touchpads do
  // when a page tears down mid-gesture.
  AbortSmoothScroll();

  CefBrowserPlatformDelegateNative::RenderViewReady();

  // The RWHV should now exist for Alloy style browsers.
  InstallRootWindowBoundsCallback();
  InstallFpsObserver();
}

bool CefBrowserPlatformDelegateNativeAura::HasFpsSample() const {
  return fps_has_sample_;
}

double CefBrowserPlatformDelegateNativeAura::GetCurrentFps() const {
  return fps_current_;
}

double CefBrowserPlatformDelegateNativeAura::GetCompositorRefreshRate() const {
  return fps_observed_compositor_ ? fps_observed_compositor_->refresh_rate() : 0.0;
}

void CefBrowserPlatformDelegateNativeAura::OnCompositingStarted(
    ui::Compositor* compositor,
    base::TimeTicks start_time) {
  if (compositor == fps_observed_compositor_) {
    RecordFrameSubmission(start_time.is_null() ? base::TimeTicks::Now()
                                               : start_time);
  }
}

void CefBrowserPlatformDelegateNativeAura::OnAnimationStep(
    base::TimeTicks timestamp) {
  TickSmoothScroll(timestamp.is_null() ? base::TimeTicks::Now() : timestamp);
}

void CefBrowserPlatformDelegateNativeAura::OnCompositingShuttingDown(
    ui::Compositor* compositor) {
  if (compositor == fps_observed_compositor_) {
    if (compositor->HasObserver(this)) {
      compositor->RemoveObserver(this);
    }
    fps_observed_compositor_ = nullptr;
  }
  if (compositor == smooth_scroll_compositor_) {
    if (compositor->HasAnimationObserver(this)) {
      compositor->RemoveAnimationObserver(this);
    }
    smooth_scroll_compositor_ = nullptr;
    ResetSmoothScrollState();
  }
}

void CefBrowserPlatformDelegateNativeAura::SendKeyEvent(
    const CefKeyEvent& event) {
  auto view = GetHostView();
  if (!view) {
    return;
  }

  ui::KeyEvent ui_event = TranslateUiKeyEvent(event);
  view->OnKeyEvent(&ui_event);
}

void CefBrowserPlatformDelegateNativeAura::SendVimbrowserBrowserCommandKeyEvent(
    const CefKeyEvent& event) {
  auto* frame = web_contents_ ? web_contents_->GetPrimaryMainFrame() : nullptr;
  auto* host = frame ? frame->GetRenderWidgetHost() : nullptr;
  if (!host) {
    return;
  }

  input::NativeWebKeyboardEvent web_event = TranslateWebKeyEvent(event);
  int modifiers =
      web_event.GetModifiers() | kVimbrowserBrowserCommandWebModifier;
  if (event.modifiers & EVENTFLAG_SHIFT_DOWN) {
    modifiers |= kVimbrowserHintNewTabWebModifier;
  }
  web_event.SetModifiers(modifiers);
  if (IsCtrlSpaceBrowserCommand(event)) {
    // Ctrl+Space is frequently delivered by X11/GTK as the control character
    // NUL. Normalize the browser-command web event here, after platform key
    // translation, so Blink can reliably recognize the semantic Space key while
    // still seeing the Control modifier that selects scrollable-hint mode.
    web_event.windows_key_code = 0x20;
    web_event.dom_code = static_cast<int>(ui::DomCode::SPACE);
    web_event.dom_key = static_cast<uint32_t>(ui::DomKey::FromCharacter(' '));
    web_event.text[0] = ' ';
    web_event.unmodified_text[0] = ' ';
  }
  web_event.is_browser_shortcut = true;
  host->ForwardKeyboardEvent(web_event);
}

void CefBrowserPlatformDelegateNativeAura::SendMouseClickEvent(
    const CefMouseEvent& event,
    CefBrowserHost::MouseButtonType type,
    bool mouseUp,
    int clickCount) {
  auto view = GetHostView();
  if (!view) {
    return;
  }

  ui::MouseEvent ui_event =
      TranslateUiClickEvent(event, type, mouseUp, clickCount);
  view->OnMouseEvent(&ui_event);
}

void CefBrowserPlatformDelegateNativeAura::SendMouseMoveEvent(
    const CefMouseEvent& event,
    bool mouseLeave) {
  auto view = GetHostView();
  if (!view) {
    return;
  }

  ui::MouseEvent ui_event = TranslateUiMoveEvent(event, mouseLeave);
  view->OnMouseEvent(&ui_event);
}

void CefBrowserPlatformDelegateNativeAura::SendMouseWheelEvent(
    const CefMouseEvent& event,
    int deltaX,
    int deltaY) {
  const bool target_viewport =
      !(event.modifiers & kVimbrowserScrollTargetElementCefModifier);
  if (smooth_scroll_scrolling_ &&
      smooth_scroll_target_viewport_ != target_viewport) {
    AbortSmoothScroll();
  }
  if (smooth_scroll_scrolling_ &&
      smooth_scroll_host_ != CurrentSmoothScrollHost()) {
    AbortSmoothScroll();
  }

  smooth_scroll_event_ = event;
  smooth_scroll_target_viewport_ = target_viewport;
  // CEF wheel deltas have the opposite sign from the qutebrowser smooth
  // scroller's content-space deltas: a negative wheel Y scrolls page content
  // down. Store the same content-space direction qutebrowser stores in m_dy.
  const int content_dx = -deltaX;
  const int content_dy = -deltaY;
  smooth_scroll_dx_ += content_dx;
  smooth_scroll_dy_ += content_dy;
  smooth_scroll_factor_ = kSmoothScrollFactor;

  if (!smooth_scroll_scrolling_) {
    smooth_scroll_subpixel_x_ = 0.0;
    smooth_scroll_subpixel_y_ = 0.0;
    if (!SendGestureScrollBegin(static_cast<float>(-content_dx),
                                static_cast<float>(-content_dy))) {
      ResetSmoothScrollState();
      return;
    }
    smooth_scroll_scrolling_ = true;
    smooth_scroll_last_tick_ = base::TimeTicks::Now();
    StartSmoothScrollAnimation();
    TickSmoothScroll(smooth_scroll_last_tick_);
  }
}

gfx::PointF CefBrowserPlatformDelegateNativeAura::SmoothScrollPosition() const {
  if (smooth_scroll_event_.x >= 0 && smooth_scroll_event_.y >= 0) {
    return gfx::PointF(static_cast<float>(smooth_scroll_event_.x),
                       static_cast<float>(smooth_scroll_event_.y));
  }
  auto* view = GetHostView();
  if (!view) {
    return gfx::PointF();
  }
  const gfx::Rect bounds = view->GetViewBounds();
  return gfx::PointF(bounds.width() / 2.0f, bounds.height() / 2.0f);
}

void CefBrowserPlatformDelegateNativeAura::StartSmoothScrollAnimation() {
  auto* view = GetHostView();
  ui::Compositor* compositor = view ? view->GetCompositor() : nullptr;
  if (!compositor) {
    return;
  }

  if (smooth_scroll_compositor_ && smooth_scroll_compositor_ != compositor &&
      smooth_scroll_compositor_->HasAnimationObserver(this)) {
    smooth_scroll_compositor_->RemoveAnimationObserver(this);
  }
  smooth_scroll_compositor_ = compositor;
  if (!smooth_scroll_compositor_->HasAnimationObserver(this)) {
    smooth_scroll_compositor_->AddAnimationObserver(this);
  }

  // A new smooth scroll is a new interactive measurement window. Do not dilute
  // it with idle compositor samples gathered before the keypress/IPC command.
  fps_frame_count_ = 0;
  fps_current_ = 0.0;
  fps_has_sample_ = false;
  fps_sample_start_ = base::TimeTicks::Now();
}

void CefBrowserPlatformDelegateNativeAura::StopSmoothScrollAnimation() {
  if (smooth_scroll_compositor_ &&
      smooth_scroll_compositor_->HasAnimationObserver(this)) {
    smooth_scroll_compositor_->RemoveAnimationObserver(this);
  }
  smooth_scroll_compositor_ = nullptr;
}

void CefBrowserPlatformDelegateNativeAura::AbortSmoothScroll() {
  StopSmoothScrollAnimation();
  ResetSmoothScrollState();
}

void CefBrowserPlatformDelegateNativeAura::ResetSmoothScrollState() {
  smooth_scroll_host_ = nullptr;
  smooth_scroll_dx_ = 0.0;
  smooth_scroll_dy_ = 0.0;
  smooth_scroll_subpixel_x_ = 0.0;
  smooth_scroll_subpixel_y_ = 0.0;
  smooth_scroll_scrolling_ = false;
  smooth_scroll_sent_begin_ = false;
  smooth_scroll_target_viewport_ = true;
}

content::RenderWidgetHost*
CefBrowserPlatformDelegateNativeAura::CurrentSmoothScrollHost() const {
  auto* view = GetHostView();
  return view ? view->host() : nullptr;
}

bool CefBrowserPlatformDelegateNativeAura::SendGestureScrollBegin(
    float deltaXHint,
    float deltaYHint) {
  auto* view = GetHostView();
  auto* host = view ? view->host() : nullptr;
  if (!host) {
    return false;
  }

  blink::WebGestureEvent event(blink::WebInputEvent::Type::kGestureScrollBegin,
                               kVimbrowserSmoothScrollWebModifier,
                               base::TimeTicks::Now());
  event.SetSourceDevice(blink::WebGestureDevice::kTouchpad);
  event.SetPositionInWidget(SmoothScrollPosition());
  event.data.scroll_begin.delta_x_hint = deltaXHint;
  event.data.scroll_begin.delta_y_hint = deltaYHint;
  event.data.scroll_begin.target_viewport = smooth_scroll_target_viewport_;
  host->ForwardGestureEvent(event);
  smooth_scroll_host_ = host;
  smooth_scroll_sent_begin_ = true;
  return true;
}

bool CefBrowserPlatformDelegateNativeAura::SendGestureScrollUpdate(int stepX,
                                                                   int stepY) {
  auto* view = GetHostView();
  auto* host = view ? view->host() : nullptr;
  if (!smooth_scroll_sent_begin_ || !smooth_scroll_host_ ||
      smooth_scroll_host_ != host) {
    return false;
  }

  blink::WebGestureEvent event(blink::WebInputEvent::Type::kGestureScrollUpdate,
                               kVimbrowserSmoothScrollWebModifier,
                               base::TimeTicks::Now());
  event.SetSourceDevice(blink::WebGestureDevice::kTouchpad);
  event.SetPositionInWidget(SmoothScrollPosition());
  event.data.scroll_update.delta_x = static_cast<float>(-stepX);
  event.data.scroll_update.delta_y = static_cast<float>(-stepY);
  host->ForwardGestureEvent(event);
  return true;
}

bool CefBrowserPlatformDelegateNativeAura::SendGestureScrollEnd() {
  auto* view = GetHostView();
  auto* host = view ? view->host() : nullptr;
  if (!smooth_scroll_sent_begin_ || !smooth_scroll_host_ ||
      smooth_scroll_host_ != host) {
    return false;
  }

  blink::WebGestureEvent event(blink::WebInputEvent::Type::kGestureScrollEnd,
                               kVimbrowserSmoothScrollWebModifier,
                               base::TimeTicks::Now());
  event.SetSourceDevice(blink::WebGestureDevice::kTouchpad);
  event.SetPositionInWidget(SmoothScrollPosition());
  host->ForwardGestureEvent(event);
  smooth_scroll_host_ = nullptr;
  smooth_scroll_sent_begin_ = false;
  return true;
}

void CefBrowserPlatformDelegateNativeAura::TickSmoothScroll(base::TimeTicks now) {
  if (!smooth_scroll_scrolling_) {
    StopSmoothScrollAnimation();
    return;
  }
  const base::TimeDelta elapsed = now - smooth_scroll_last_tick_;
  smooth_scroll_last_tick_ = now;
  const double dt = std::max(1.0, elapsed.InMillisecondsF());

  if (!smooth_scroll_sent_begin_ ||
      smooth_scroll_host_ != CurrentSmoothScrollHost()) {
    AbortSmoothScroll();
    return;
  }

  const double effective_factor =
      1.0 - std::pow(1.0 - smooth_scroll_factor_, dt / 16.0);

  const double frac_step_x = smooth_scroll_dx_ * effective_factor;
  const double frac_step_y = smooth_scroll_dy_ * effective_factor;

  smooth_scroll_subpixel_x_ += frac_step_x;
  smooth_scroll_subpixel_y_ += frac_step_y;

  const int step_x = static_cast<int>(smooth_scroll_subpixel_x_);
  const int step_y = static_cast<int>(smooth_scroll_subpixel_y_);

  smooth_scroll_subpixel_x_ -= step_x;
  smooth_scroll_subpixel_y_ -= step_y;
  smooth_scroll_dx_ -= frac_step_x;
  smooth_scroll_dy_ -= frac_step_y;

  if (step_x != 0 || step_y != 0) {
    if (!SendGestureScrollUpdate(step_x, step_y)) {
      AbortSmoothScroll();
      return;
    }
    // The synthetic gesture update is the frame-driving unit for our
    // qutebrowser-style compositor scroll path. Count it directly instead of
    // opening a DevTools trace and mining DrawFrame/BeginFrame events. This is
    // intentionally local and cheap, and it reflects the cadence at which the
    // backend is feeding Chromium's compositor/input pipeline during smooth
    // scrolling.
    RecordFrameSubmission(now);
  }

  if (std::abs(smooth_scroll_dx_) < 0.01 &&
      std::abs(smooth_scroll_dy_) < 0.01) {
    StopSmoothScrollAnimation();
    SendGestureScrollEnd();
    ResetSmoothScrollState();
  }
}

void CefBrowserPlatformDelegateNativeAura::SendTouchEvent(
    const CefTouchEvent& event) {
  NOTIMPLEMENTED();
}

std::unique_ptr<CefMenuRunner>
CefBrowserPlatformDelegateNativeAura::CreateMenuRunner() {
  return base::WrapUnique(new CefMenuRunnerViewsAura);
}

gfx::Point CefBrowserPlatformDelegateNativeAura::GetScreenPoint(
    const gfx::Point& view,
    bool want_dip_coords) const {
  if (windowless_handler_) {
    return windowless_handler_->GetParentScreenPoint(view, want_dip_coords);
  }

  if (!window_widget_) {
    return view;
  }

  gfx::Point screen_pt(view);
  if (!view_util::ConvertPointToScreen(
          window_widget_->GetRootView(), &screen_pt,
          /*output_pixel_coords=*/!want_dip_coords)) {
    return view;
  }

  return screen_pt;
}

input::NativeWebKeyboardEvent
CefBrowserPlatformDelegateNativeAura::TranslateWebKeyEvent(
    const CefKeyEvent& key_event) const {
  return input::NativeWebKeyboardEvent(TranslateUiKeyEvent(key_event));
}

blink::WebMouseEvent
CefBrowserPlatformDelegateNativeAura::TranslateWebClickEvent(
    const CefMouseEvent& mouse_event,
    CefBrowserHost::MouseButtonType type,
    bool mouseUp,
    int clickCount) const {
  return ui::MakeWebMouseEvent(
      TranslateUiClickEvent(mouse_event, type, mouseUp, clickCount));
}

blink::WebMouseEvent
CefBrowserPlatformDelegateNativeAura::TranslateWebMoveEvent(
    const CefMouseEvent& mouse_event,
    bool mouseLeave) const {
  return ui::MakeWebMouseEvent(TranslateUiMoveEvent(mouse_event, mouseLeave));
}

blink::WebMouseWheelEvent
CefBrowserPlatformDelegateNativeAura::TranslateWebWheelEvent(
    const CefMouseEvent& mouse_event,
    int deltaX,
    int deltaY) const {
  return ui::MakeWebMouseWheelEvent(
      TranslateUiWheelEvent(mouse_event, deltaX, deltaY));
}

ui::MouseEvent CefBrowserPlatformDelegateNativeAura::TranslateUiClickEvent(
    const CefMouseEvent& mouse_event,
    CefBrowserHost::MouseButtonType type,
    bool mouseUp,
    int clickCount) const {
  DCHECK_GE(clickCount, 1);

  ui::EventType event_type =
      mouseUp ? ui::EventType::kMouseReleased : ui::EventType::kMousePressed;
  gfx::PointF location(mouse_event.x, mouse_event.y);
  gfx::PointF root_location(GetScreenPoint(
      gfx::Point(mouse_event.x, mouse_event.y), /*want_dip_coords=*/false));
  base::TimeTicks time_stamp = GetEventTimeStamp();
  int flags = TranslateUiEventModifiers(mouse_event.modifiers);

  int changed_button_flags = 0;
  switch (type) {
    case MBT_LEFT:
      changed_button_flags |= ui::EF_LEFT_MOUSE_BUTTON;
      break;
    case MBT_MIDDLE:
      changed_button_flags |= ui::EF_MIDDLE_MOUSE_BUTTON;
      break;
    case MBT_RIGHT:
      changed_button_flags |= ui::EF_RIGHT_MOUSE_BUTTON;
      break;
    default:
      DCHECK(false);
  }

  ui::MouseEvent result(event_type, location, root_location, time_stamp, flags,
                        changed_button_flags);
  result.SetClickCount(clickCount);
  return result;
}

ui::MouseEvent CefBrowserPlatformDelegateNativeAura::TranslateUiMoveEvent(
    const CefMouseEvent& mouse_event,
    bool mouseLeave) const {
  ui::EventType event_type =
      mouseLeave ? ui::EventType::kMouseExited : ui::EventType::kMouseMoved;
  gfx::PointF location(mouse_event.x, mouse_event.y);
  gfx::PointF root_location(GetScreenPoint(
      gfx::Point(mouse_event.x, mouse_event.y), /*want_dip_coords=*/false));
  base::TimeTicks time_stamp = GetEventTimeStamp();
  int flags = TranslateUiEventModifiers(mouse_event.modifiers);

  int changed_button_flags = 0;
  if (!mouseLeave) {
    changed_button_flags = TranslateUiChangedButtonFlags(mouse_event.modifiers);
  }

  return ui::MouseEvent(event_type, location, root_location, time_stamp, flags,
                        changed_button_flags);
}

ui::MouseWheelEvent CefBrowserPlatformDelegateNativeAura::TranslateUiWheelEvent(
    const CefMouseEvent& mouse_event,
    int deltaX,
    int deltaY) const {
  gfx::Vector2d offset(GetUiWheelEventOffset(deltaX, deltaY));

  gfx::PointF location(mouse_event.x, mouse_event.y);
  gfx::PointF root_location(GetScreenPoint(
      gfx::Point(mouse_event.x, mouse_event.y), /*want_dip_coords=*/false));
  base::TimeTicks time_stamp = GetEventTimeStamp();
  int flags = TranslateUiEventModifiers(mouse_event.modifiers);
  int changed_button_flags =
      TranslateUiChangedButtonFlags(mouse_event.modifiers);

  return ui::MouseWheelEvent(offset, location, root_location, time_stamp, flags,
                             changed_button_flags);
}

gfx::Vector2d CefBrowserPlatformDelegateNativeAura::GetUiWheelEventOffset(
    int deltaX,
    int deltaY) const {
  return gfx::Vector2d(deltaX, deltaY);
}

base::OnceClosure
CefBrowserPlatformDelegateNativeAura::GetWidgetDeleteCallback() {
  return base::BindOnce(&CefBrowserPlatformDelegateNativeAura::WidgetDeleted,
                        weak_ptr_factory_.GetWeakPtr());
}

// static
base::TimeTicks CefBrowserPlatformDelegateNativeAura::GetEventTimeStamp() {
  return base::TimeTicks::Now();
}

// static
int CefBrowserPlatformDelegateNativeAura::TranslateUiEventModifiers(
    uint32_t cef_modifiers) {
  int result = 0;
  // Set modifiers based on key state.
  if (cef_modifiers & EVENTFLAG_CAPS_LOCK_ON) {
    result |= ui::EF_CAPS_LOCK_ON;
  }
  if (cef_modifiers & EVENTFLAG_SHIFT_DOWN) {
    result |= ui::EF_SHIFT_DOWN;
  }
  if (cef_modifiers & EVENTFLAG_CONTROL_DOWN) {
    result |= ui::EF_CONTROL_DOWN;
  }
  if (cef_modifiers & EVENTFLAG_ALT_DOWN) {
    result |= ui::EF_ALT_DOWN;
  }
  if (cef_modifiers & EVENTFLAG_LEFT_MOUSE_BUTTON) {
    result |= ui::EF_LEFT_MOUSE_BUTTON;
  }
  if (cef_modifiers & EVENTFLAG_MIDDLE_MOUSE_BUTTON) {
    result |= ui::EF_MIDDLE_MOUSE_BUTTON;
  }
  if (cef_modifiers & EVENTFLAG_RIGHT_MOUSE_BUTTON) {
    result |= ui::EF_RIGHT_MOUSE_BUTTON;
  }
  if (cef_modifiers & EVENTFLAG_COMMAND_DOWN) {
    result |= ui::EF_COMMAND_DOWN;
  }
  if (cef_modifiers & EVENTFLAG_NUM_LOCK_ON) {
    result |= ui::EF_NUM_LOCK_ON;
  }
  if (cef_modifiers & EVENTFLAG_IS_KEY_PAD) {
    result |= ui::EF_IS_EXTENDED_KEY;
  }
  if (cef_modifiers & EVENTFLAG_ALTGR_DOWN) {
    result |= ui::EF_ALTGR_DOWN;
  }
  if (cef_modifiers & EVENTFLAG_IS_REPEAT) {
    result |= ui::EF_IS_REPEAT;
  }
  if (cef_modifiers & EVENTFLAG_PRECISION_SCROLLING_DELTA) {
    result |= ui::EF_PRECISION_SCROLLING_DELTA;
  }
  if (cef_modifiers & EVENTFLAG_SCROLL_BY_PAGE) {
    result |= ui::EF_SCROLL_BY_PAGE;
  }

  return result;
}

// static
int CefBrowserPlatformDelegateNativeAura::TranslateUiChangedButtonFlags(
    uint32_t cef_modifiers) {
  int result = 0;
  if (cef_modifiers & EVENTFLAG_LEFT_MOUSE_BUTTON) {
    result |= ui::EF_LEFT_MOUSE_BUTTON;
  } else if (cef_modifiers & EVENTFLAG_MIDDLE_MOUSE_BUTTON) {
    result |= ui::EF_MIDDLE_MOUSE_BUTTON;
  } else if (cef_modifiers & EVENTFLAG_RIGHT_MOUSE_BUTTON) {
    result |= ui::EF_RIGHT_MOUSE_BUTTON;
  }
  return result;
}

void CefBrowserPlatformDelegateNativeAura::WidgetDeleted() {
  DCHECK(window_widget_);
  window_widget_ = nullptr;
}

content::RenderWidgetHostViewAura*
CefBrowserPlatformDelegateNativeAura::GetHostView() const {
  if (!web_contents_) {
    return nullptr;
  }
  return static_cast<content::RenderWidgetHostViewAura*>(
      web_contents_->GetRenderWidgetHostView());
}

void CefBrowserPlatformDelegateNativeAura::InstallFpsObserver() {
  auto* view = GetHostView();
  ui::Compositor* compositor = view ? view->GetCompositor() : nullptr;
  if (!compositor || compositor == fps_observed_compositor_) {
    return;
  }

  RemoveFpsObserver();

  fps_observed_compositor_ = compositor;
  fps_frame_count_ = 0;
  fps_current_ = 0.0;
  fps_has_sample_ = false;
  fps_sample_start_ = base::TimeTicks::Now();
  compositor->AddObserver(this);
  // Local browser-compositor callbacks: no DevTools tracing session, JSON
  // parsing, global trace buffer, or renderer round-trip.
}

void CefBrowserPlatformDelegateNativeAura::RemoveFpsObserver() {
  if (fps_observed_compositor_) {
    if (fps_observed_compositor_->HasObserver(this)) {
      fps_observed_compositor_->RemoveObserver(this);
    }
    fps_observed_compositor_ = nullptr;
  }
}

void CefBrowserPlatformDelegateNativeAura::RecordFrameSubmission(
    base::TimeTicks now) {
  if (fps_sample_start_.is_null()) {
    fps_sample_start_ = now;
  }
  ++fps_frame_count_;
  const base::TimeDelta elapsed = now - fps_sample_start_;
  if (elapsed >= base::Seconds(1)) {
    fps_current_ = fps_frame_count_ / elapsed.InSecondsF();
    fps_frame_count_ = 0;
    fps_sample_start_ = now;
    fps_has_sample_ = true;
  }
}
