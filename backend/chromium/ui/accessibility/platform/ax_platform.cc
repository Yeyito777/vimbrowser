// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/accessibility/platform/ax_platform.h"

#include "base/check_op.h"
#include "ui/accessibility/accessibility_features.h"
#include "ui/accessibility/platform/ax_mode_observer.h"


namespace ui {

namespace {

AXPlatform* g_instance = nullptr;

}  // namespace

// static
AXPlatform& AXPlatform::GetInstance() {
  CHECK_NE(g_instance, nullptr)
      << "AXPlatform::GetInstance() called before AXPlatform was initialized "
         "or destroyed. If you are in a browser test, you may need cleanup in "
         "TearDownOnMainThread().";
  DCHECK_CALLED_ON_VALID_THREAD(g_instance->thread_checker_);
  return *g_instance;
}

AXPlatform::AXPlatform(Delegate& delegate) : delegate_(delegate) {
  DCHECK_EQ(g_instance, nullptr);
  g_instance = this;
}

AXPlatform::~AXPlatform() {
  DCHECK_EQ(g_instance, this);
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  g_instance = nullptr;
}

AXMode AXPlatform::GetMode() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return delegate_->GetAccessibilityMode();
}

void AXPlatform::AddModeObserver(AXModeObserver* observer) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  observers_.AddObserver(observer);
}

void AXPlatform::RemoveModeObserver(AXModeObserver* observer) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  observers_.RemoveObserver(observer);
}

void AXPlatform::NotifyModeAdded(AXMode mode) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  observers_.Notify(&AXModeObserver::OnAXModeAdded, mode);
}

void AXPlatform::NotifyAssistiveTechChanged(AssistiveTech assistive_tech) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (active_assistive_tech_ == assistive_tech) {
    return;
  }
  active_assistive_tech_ = assistive_tech;
  observers_.Notify(&AXModeObserver::OnAssistiveTechChanged, assistive_tech);
}

bool AXPlatform::IsScreenReaderActive() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return IsScreenReader(active_assistive_tech_);
}

bool AXPlatform::IsCaretBrowsingEnabled() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  return caret_browsing_enabled_;
}

void AXPlatform::SetCaretBrowsingState(bool enabled) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  caret_browsing_enabled_ = enabled;
}



void AXPlatform::OnMinimalPropertiesUsed(bool is_name_used) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  delegate_->OnMinimalPropertiesUsed();
}

void AXPlatform::OnPropertiesUsedInBrowserUI() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  delegate_->OnPropertiesUsedInBrowserUI();
}

void AXPlatform::OnPropertiesUsedInWebContent() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  delegate_->OnPropertiesUsedInWebContent();
}

void AXPlatform::OnInlineTextBoxesUsedInWebContent() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  delegate_->OnInlineTextBoxesUsedInWebContent();
}

void AXPlatform::OnExtendedPropertiesUsedInWebContent() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  delegate_->OnExtendedPropertiesUsedInWebContent();
}

void AXPlatform::OnHTMLAttributesUsed() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  delegate_->OnHTMLAttributesUsed();
}

void AXPlatform::OnActionFromAssistiveTech() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  delegate_->OnActionFromAssistiveTech();
}

void AXPlatform::DetachFromThreadForTesting() {
  DETACH_FROM_THREAD(thread_checker_);
}


}  // namespace ui
