// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SETTINGS_ABOUT_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_SETTINGS_ABOUT_HANDLER_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/ui/webui/help/version_updater.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"
#include "chrome/browser/upgrade_detector/upgrade_observer.h"
#include "components/policy/core/common/policy_service.h"
#include "content/public/browser/web_ui_message_handler.h"


namespace base {
class Clock;
class FilePath;
}  // namespace base

class Profile;

namespace settings {

// WebUI message handler for the help page.
class AboutHandler : public settings::SettingsPageUIHandler,
                     public UpgradeObserver {
 public:
  explicit AboutHandler(Profile* profile);

  AboutHandler(const AboutHandler&) = delete;
  AboutHandler& operator=(const AboutHandler&) = delete;

  ~AboutHandler() override;

  // WebUIMessageHandler implementation.
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // UpgradeObserver implementation.
  void OnUpgradeRecommended() override;

 protected:
  // Used to test the EOL string displayed in the About details page.
  void set_clock(base::Clock* clock) { clock_ = clock; }

 private:
  void OnDeviceAutoUpdatePolicyChanged(const base::Value* previous_policy,
                                       const base::Value* current_policy);

  // Called once the JS page is ready to be called, serves as a signal to the
  // handler to register C++ observers.
  void HandlePageReady(const base::ListValue& args);

  // Called once when the page has loaded. On ChromeOS, this gets the current
  // update status. On other platforms, it will request and perform an update
  // (if one is available).
  void HandleRefreshUpdateStatus(const base::ListValue& args);
  void RefreshUpdateStatus();

#if BUILDFLAG(IS_MAC)
  // Promotes the updater for all users.
  void PromoteUpdater(const base::ListValue& args);
#endif

  // Opens the feedback dialog.
  // |args| must be empty.
  void HandleOpenFeedbackDialog(const base::ListValue& args);

  // Opens the help page. |args| must be empty.
  void HandleOpenHelpPage(const base::ListValue& args);


  // Checks for and applies update.
  void RequestUpdate();

  // Callback method which forwards status updates to the page.
  void SetUpdateStatus(VersionUpdater::Status status,
                       int progress,
                       bool rollback,
                       bool powerwash,
                       const std::string& version,
                       int64_t size,
                       const std::u16string& fail_message);

#if BUILDFLAG(IS_MAC)
  // Callback method which forwards promotion state to the page.
  void SetPromotionState(VersionUpdater::PromotionState state);
#endif


  const raw_ptr<Profile> profile_;

  // Specialized instance of the VersionUpdater used to update the browser.
  std::unique_ptr<VersionUpdater> version_updater_;

  // Used to observe changes in the |kDeviceAutoUpdateDisabled| policy.
  std::unique_ptr<policy::PolicyChangeRegistrar> policy_registrar_;

  // If true changes to UpgradeObserver are applied, if false they are ignored.
  bool apply_changes_from_upgrade_observer_ = false;

  // Override to test the EOL string displayed in the About details page.
  raw_ptr<base::Clock> clock_;

  // Used for callbacks.
  base::WeakPtrFactory<AboutHandler> weak_factory_{this};
};

}  // namespace settings

#endif  // CHROME_BROWSER_UI_WEBUI_SETTINGS_ABOUT_HANDLER_H_
