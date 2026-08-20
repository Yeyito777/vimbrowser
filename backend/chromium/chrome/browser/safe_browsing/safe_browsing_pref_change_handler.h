// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SAFE_BROWSING_SAFE_BROWSING_PREF_CHANGE_HANDLER_H_
#define CHROME_BROWSER_SAFE_BROWSING_SAFE_BROWSING_PREF_CHANGE_HANDLER_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"


#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_MAC)
#include "chrome/browser/ui/toasts/toast_controller.h"
#endif

class Profile;

namespace safe_browsing {

// Handles showing the appropriate toast or modal when the Safe Browsing
// protection setting changes. This class is not thread-safe.
class SafeBrowsingPrefChangeHandler {
 public:
  explicit SafeBrowsingPrefChangeHandler(Profile* profile);
  virtual ~SafeBrowsingPrefChangeHandler();

  // The amount of time to wait after construction before checking if a retry is
  // needed.
  static constexpr const base::TimeDelta kRetryAttemptStartupDelay =
      base::Minutes(2);

  // The amount of time to wait between retry attempts.
  static constexpr const base::TimeDelta kRetryNextAttemptDelay = base::Days(1);

  // Length of time that the retry mechanism will wait before running. This
  // delay is used for the case where the safe browsing pref change handler
  // can't tell if it succeeded in the past.
  static constexpr const base::TimeDelta kWaitingPeriodInterval = base::Days(2);

  // Handles notifying the user when necessary. The type of notification shown
  // depends on the platform and whether the user is currently on the security
  // settings page. Virtual for tests.
  virtual void MaybeShowEnhancedProtectionSettingChangeNotification();

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_MAC)
  void SetToastControllerForTesting(ToastController* controller);
#endif

 private:
  // Member variable to store the Profile*.
  raw_ptr<Profile> profile_;

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_WIN) || \
    BUILDFLAG(IS_MAC)
  raw_ptr<ToastController> toast_controller_for_testing_ = nullptr;
#endif

};

}  // namespace safe_browsing

#endif  // CHROME_BROWSER_SAFE_BROWSING_SAFE_BROWSING_PREF_CHANGE_HANDLER_H_
