// Copyright 2016 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#ifndef CEF_LIBCEF_BROWSER_VIEWS_TEXTFIELD_VIEW_H_
#define CEF_LIBCEF_BROWSER_VIEWS_TEXTFIELD_VIEW_H_
#pragma once

#include <optional>

#include "cef/include/views/cef_textfield.h"
#include "cef/include/views/cef_textfield_delegate.h"
#include "cef/libcef/browser/views/view_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/controls/textfield/textfield_controller.h"

class CefTextfieldView
    : public CefViewView<views::Textfield, CefTextfieldDelegate>,
      public views::TextfieldController {
  METADATA_HEADER(CefTextfieldView, views::Textfield)

 public:
  using ParentClass = CefViewView<views::Textfield, CefTextfieldDelegate>;

  // |cef_delegate| may be nullptr.
  explicit CefTextfieldView(CefTextfieldDelegate* cef_delegate);

  CefTextfieldView(const CefTextfieldView&) = delete;
  CefTextfieldView& operator=(const CefTextfieldView&) = delete;

  void Initialize() override;

  // Keep the configured background separate from the transient hover
  // background so app-side restyles remain correct while the pointer is over
  // the field.
  void SetCefBackgroundColor(SkColor color);
  SkColor GetCefBackgroundColor() const;
  void SetHoverBackgroundColor(SkColor color);

  // Returns the CefTextfield associated with this view. See comments on
  // CefViewView::GetCefView.
  CefRefPtr<CefTextfield> GetCefTextfield() const {
    CefRefPtr<CefTextfield> textfield = GetCefView()->AsTextfield();
    DCHECK(textfield);
    return textfield;
  }

  // TextfieldController methods:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;
  void OnAfterUserAction(views::Textfield* sender) override;

  // views::Textfield methods:
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

 private:
  std::optional<SkColor> configured_background_color_;
  std::optional<SkColor> hover_background_color_;
  bool mouse_hovered_ = false;
};

#endif  // CEF_LIBCEF_BROWSER_VIEWS_TEXTFIELD_VIEW_H_
