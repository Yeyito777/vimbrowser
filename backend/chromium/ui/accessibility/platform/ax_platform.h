// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_ACCESSIBILITY_PLATFORM_AX_PLATFORM_H_
#define UI_ACCESSIBILITY_PLATFORM_AX_PLATFORM_H_

#include "base/component_export.h"
#include "base/gtest_prod_util.h"
#include "base/memory/raw_ref.h"
#include "base/observer_list.h"
#include "base/scoped_observation_traits.h"
#include "base/threading/thread_checker.h"
#include "build/build_config.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/platform/assistive_tech.h"

namespace ui {

class AXModeObserver;
class AXPlatformNode;

// Process-wide accessibility platform state.
class COMPONENT_EXPORT(AX_PLATFORM) AXPlatform {
 public:

  class COMPONENT_EXPORT(AX_PLATFORM) Delegate {
   public:
    Delegate(const Delegate&) = delete;
    Delegate& operator=(const Delegate&) = delete;
    virtual ~Delegate() = default;

    // Returns the effective process-wide accessibility mode.
    virtual AXMode GetAccessibilityMode() = 0;


    // A very basic accessible property was used, such as role, name or
    // location. Only enables AXMode::kNativeAPIs unless the screen reader
    // honeypot is used.
    virtual void OnMinimalPropertiesUsed() {}
    // An a11y property was used in the browser UI. Enable AXMode::kNativeAPIs.
    virtual void OnPropertiesUsedInBrowserUI() {}
    // A basic property was used in web content. Enable AXMode::kWebContents.
    virtual void OnPropertiesUsedInWebContent() {}
    // Inline textboxes were used. Enable AXMode::kInlineTextBoxes.
    virtual void OnInlineTextBoxesUsedInWebContent() {}
    // Extended properties were used. Enable AXMode::kExtendedProperties.
    virtual void OnExtendedPropertiesUsedInWebContent() {}
    // HTML properties were used. Enable AXMode::kHTML.
    virtual void OnHTMLAttributesUsed() {}
    // An a11y action was used in web content.
    virtual void OnActionFromAssistiveTech() {}

   protected:
    Delegate() = default;
  };

  // Returns the single process-wide instance.
  static AXPlatform& GetInstance();

  // Constructs a new instance. Only one instance may be alive in a process at
  // any given time. Typically, the embedder creates one during process startup
  // and ensures that it is kept alive throughout the process's UX.
  explicit AXPlatform(Delegate& delegate);
  AXPlatform(const AXPlatform&) = delete;
  AXPlatform& operator=(const AXPlatform&) = delete;
  ~AXPlatform();

  // Returns the process-wide accessibility mode.
  AXMode GetMode();

  void AddModeObserver(AXModeObserver* observer);
  void RemoveModeObserver(AXModeObserver* observer);

  // Notifies observers that the mode flags in `mode` have been added to the
  // process-wide accessibility mode.
  void NotifyModeAdded(AXMode mode);

  // Notify observers that an assistive technology was launched or exited.
  // Note: in some cases we do not yet have a perfect signal when the user
  // quits their assistive tech, so in that case the tool will continue to
  // appear to be present.
  // The only known assistive tech that this affects currently is JAWS.
  // TODO(crbug.com/402069423) Improve JAWS exit detection.
  void NotifyAssistiveTechChanged(AssistiveTech assistive_tech);

  // The current active assistive tech, such as a screen reader, where a
  // detection algorithm has been implemented.
  AssistiveTech active_assistive_tech() const {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    return active_assistive_tech_;
  }

  // Is the current active assistive tech a screen reader.
  bool IsScreenReaderActive();

  // Returns whether caret browsing is enabled. When caret browsing is enabled,
  // we need to ensure that we keep ATs aware of caret movement.
  bool IsCaretBrowsingEnabled();
  void SetCaretBrowsingState(bool enabled);


  // A very basic accessible property was used, such as role, name or location.
  // Always enables AXMode::kNativeAPIs by calling OnMinimalPropertiesUsed() on
  // the delegate. If the screen reader honeypot is used (currently windows
  // only), OnPropertiesUsedInWebContent() will also be called, enabling web
  // content accessibility via AXMode::kWebContents.
  void OnMinimalPropertiesUsed(bool is_name_used = false);
  // An a11y property was used in the browser UI. Enable AXMode::kNativeAPIs.
  void OnPropertiesUsedInBrowserUI();
  // A basic property was used in web content. Enable AXMode::kWebContents.
  void OnPropertiesUsedInWebContent();
  // Inline textboxes were used. Enable AXMode::kInlineTextBoxes.
  void OnInlineTextBoxesUsedInWebContent();
  // Extended properties were used. Enable AXMode::kExtendedProperties.
  void OnExtendedPropertiesUsedInWebContent();
  // HTML properties were used. Enable AXMode::kHTML.
  void OnHTMLAttributesUsed();
  // An a11y action was used in web content.
  void OnActionFromAssistiveTech();

  void DetachFromThreadForTesting();

 private:
  friend class ::ui::AXPlatformNode;
  FRIEND_TEST_ALL_PREFIXES(AXPlatformTest, Observer);


  // The embedder's delegate.
  const raw_ref<Delegate> delegate_ GUARDED_BY_CONTEXT(thread_checker_);

  base::ObserverList<AXModeObserver,
                     /*check_empty=*/true,
                     base::ObserverListReentrancyPolicy::kDisallowReentrancy>
      observers_ GUARDED_BY_CONTEXT(thread_checker_);


  // Keeps track of the active AssistiveTech.
  AssistiveTech active_assistive_tech_ GUARDED_BY_CONTEXT(thread_checker_) =
      AssistiveTech::kUninitialized;

  // Keeps track of whether caret browsing is enabled.
  bool caret_browsing_enabled_ GUARDED_BY_CONTEXT(thread_checker_) = false;


  THREAD_CHECKER(thread_checker_);
};

}  // namespace ui

namespace base {

// Traits type in support of base::ScopedObservation.
template <>
struct ScopedObservationTraits<ui::AXPlatform, ui::AXModeObserver> {
  static void AddObserver(ui::AXPlatform* source,
                          ui::AXModeObserver* observer) {
    source->AddModeObserver(observer);
  }
  static void RemoveObserver(ui::AXPlatform* source,
                             ui::AXModeObserver* observer) {
    source->RemoveModeObserver(observer);
  }
};

}  // namespace base

#endif  // UI_ACCESSIBILITY_PLATFORM_AX_PLATFORM_H_
