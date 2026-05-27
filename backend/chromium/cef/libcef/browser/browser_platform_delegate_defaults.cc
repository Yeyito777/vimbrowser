// Copyright 2015 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/browser/browser_platform_delegate.h"

#include "base/check.h"
#include "base/notimplemented.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/size.h"

bool CefBrowserPlatformDelegate::CreateHostWindow() {
  DCHECK(false);
  return true;
}

void CefBrowserPlatformDelegate::CloseHostWindow() {
  DCHECK(false);
}

CefWindowHandle CefBrowserPlatformDelegate::GetHostWindowHandle() const {
  DCHECK(false);
  return kNullWindowHandle;
}

views::Widget* CefBrowserPlatformDelegate::GetWindowWidget() const {
  DCHECK(false);
  return nullptr;
}

CefRefPtr<CefBrowserView> CefBrowserPlatformDelegate::GetBrowserView() const {
  return nullptr;
}

void CefBrowserPlatformDelegate::SetBrowserView(
    CefRefPtr<CefBrowserView> browser_view) {
  DCHECK(false);
}

web_modal::WebContentsModalDialogHost*
CefBrowserPlatformDelegate::GetWebContentsModalDialogHost() const {
  DCHECK(false);
  return nullptr;
}

SkColor CefBrowserPlatformDelegate::GetBackgroundColor() const {
  DCHECK(false);
  return SkColor();
}

void CefBrowserPlatformDelegate::WasResized() {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::SendKeyEvent(const CefKeyEvent& event) {
  NOTIMPLEMENTED();
}

void CefBrowserPlatformDelegate::SendMouseClickEvent(
    const CefMouseEvent& event,
    CefBrowserHost::MouseButtonType type,
    bool mouseUp,
    int clickCount) {
  NOTIMPLEMENTED();
}

void CefBrowserPlatformDelegate::SendMouseMoveEvent(const CefMouseEvent& event,
                                                    bool mouseLeave) {
  NOTIMPLEMENTED();
}

void CefBrowserPlatformDelegate::SendMouseWheelEvent(const CefMouseEvent& event,
                                                     int deltaX,
                                                     int deltaY) {
  NOTIMPLEMENTED();
}

void CefBrowserPlatformDelegate::SendTouchEvent(const CefTouchEvent& event) {
  NOTIMPLEMENTED();
}

void CefBrowserPlatformDelegate::SetFocus(bool setFocus) {}

void CefBrowserPlatformDelegate::SendCaptureLostEvent() {
  NOTIMPLEMENTED();
}

#if BUILDFLAG(IS_WIN) || (BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_MAC))
void CefBrowserPlatformDelegate::NotifyMoveOrResizeStarted() {}

void CefBrowserPlatformDelegate::SizeTo(int width, int height) {}
#endif

gfx::Point CefBrowserPlatformDelegate::GetScreenPoint(
    const gfx::Point& view,
    bool want_dip_coords) const {
  DCHECK(false);
  return gfx::Point();
}

void CefBrowserPlatformDelegate::ViewText(const std::string& text) {
  NOTIMPLEMENTED();
}

bool CefBrowserPlatformDelegate::HandleKeyboardEvent(
    const input::NativeWebKeyboardEvent& event) {
  DCHECK(false);
  return false;
}

CefEventHandle CefBrowserPlatformDelegate::GetEventHandle(
    const input::NativeWebKeyboardEvent& event) const {
  DCHECK(false);
  return kNullEventHandle;
}

std::unique_ptr<CefJavaScriptDialogRunner>
CefBrowserPlatformDelegate::CreateJavaScriptDialogRunner() {
  return nullptr;
}

std::unique_ptr<CefMenuRunner> CefBrowserPlatformDelegate::CreateMenuRunner() {
  NOTIMPLEMENTED();
  return nullptr;
}

bool CefBrowserPlatformDelegate::IsWindowless() const {
  return false;
}

bool CefBrowserPlatformDelegate::IsViewsHosted() const {
  return false;
}

bool CefBrowserPlatformDelegate::HasExternalParent() const {
  // In the majority of cases a Views-hosted browser will not have an external
  // parent, and visa-versa.
  return !IsViewsHosted();
}

void CefBrowserPlatformDelegate::WasHidden(bool hidden) {
  DCHECK(false);
}

bool CefBrowserPlatformDelegate::IsHidden() const {
  DCHECK(false);
  return false;
}

void CefBrowserPlatformDelegate::NotifyScreenInfoChanged() {}

void CefBrowserPlatformDelegate::Invalidate(cef_paint_element_type_t type) {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::SendExternalBeginFrame() {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::SetWindowlessFrameRate(int frame_rate) {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::ImeSetComposition(
    const CefString& text,
    const std::vector<CefCompositionUnderline>& underlines,
    const CefRange& replacement_range,
    const CefRange& selection_range) {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::ImeCommitText(
    const CefString& text,
    const CefRange& replacement_range,
    int relative_cursor_pos) {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::ImeFinishComposingText(bool keep_selection) {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::ImeCancelComposition() {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::DragTargetDragEnter(
    CefRefPtr<CefDragData> drag_data,
    const CefMouseEvent& event,
    cef_drag_operations_mask_t allowed_ops) {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::DragTargetDragOver(
    const CefMouseEvent& event,
    cef_drag_operations_mask_t allowed_ops) {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::DragTargetDragLeave() {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::DragTargetDrop(const CefMouseEvent& event) {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::StartDragging(
    const content::DropData& drop_data,
    blink::DragOperationsMask allowed_ops,
    const gfx::ImageSkia& image,
    const gfx::Vector2d& image_offset,
    const blink::mojom::DragEventSourceInfo& event_info,
    content::RenderWidgetHostImpl* source_rwh) {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::UpdateDragOperation(
    ui::mojom::DragOperation operation,
    bool document_is_handling_drag) {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::DragSourceEndedAt(
    int x,
    int y,
    cef_drag_operations_mask_t op) {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::DragSourceSystemDragEnded() {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::AccessibilityEventReceived(
    const ui::AXUpdatesAndEvents& details) {
  DCHECK(false);
}

void CefBrowserPlatformDelegate::AccessibilityLocationChangesReceived(
    const ui::AXTreeID& tree_id,
    ui::AXLocationAndScrollUpdates& details) {
  DCHECK(false);
}

gfx::Point CefBrowserPlatformDelegate::GetDialogPosition(
    const gfx::Size& size) {
  const gfx::Size& max_size = GetMaximumDialogSize();
  return gfx::Point((max_size.width() - size.width()) / 2,
                    (max_size.height() - size.height()) / 2);
}

// static
int CefBrowserPlatformDelegate::TranslateWebEventModifiers(
    uint32_t cef_modifiers) {
  int result = 0;
  // Set modifiers based on key state.
  if (cef_modifiers & EVENTFLAG_CAPS_LOCK_ON) {
    result |= blink::WebInputEvent::kCapsLockOn;
  }
  if (cef_modifiers & EVENTFLAG_SHIFT_DOWN) {
    result |= blink::WebInputEvent::kShiftKey;
  }
  if (cef_modifiers & EVENTFLAG_CONTROL_DOWN) {
    result |= blink::WebInputEvent::kControlKey;
  }
  if (cef_modifiers & EVENTFLAG_ALT_DOWN) {
    result |= blink::WebInputEvent::kAltKey;
  }
  if (cef_modifiers & EVENTFLAG_LEFT_MOUSE_BUTTON) {
    result |= blink::WebInputEvent::kLeftButtonDown;
  }
  if (cef_modifiers & EVENTFLAG_MIDDLE_MOUSE_BUTTON) {
    result |= blink::WebInputEvent::kMiddleButtonDown;
  }
  if (cef_modifiers & EVENTFLAG_RIGHT_MOUSE_BUTTON) {
    result |= blink::WebInputEvent::kRightButtonDown;
  }
  if (cef_modifiers & EVENTFLAG_COMMAND_DOWN) {
    result |= blink::WebInputEvent::kMetaKey;
  }
  if (cef_modifiers & EVENTFLAG_NUM_LOCK_ON) {
    result |= blink::WebInputEvent::kNumLockOn;
  }
  if (cef_modifiers & EVENTFLAG_IS_KEY_PAD) {
    result |= blink::WebInputEvent::kIsKeyPad;
  }
  if (cef_modifiers & EVENTFLAG_IS_LEFT) {
    result |= blink::WebInputEvent::kIsLeft;
  }
  if (cef_modifiers & EVENTFLAG_IS_RIGHT) {
    result |= blink::WebInputEvent::kIsRight;
  }
  if (cef_modifiers & EVENTFLAG_ALTGR_DOWN) {
    result |= blink::WebInputEvent::kAltGrKey;
  }
  if (cef_modifiers & EVENTFLAG_IS_REPEAT) {
    result |= blink::WebInputEvent::kIsAutoRepeat;
  }
  return result;
}
