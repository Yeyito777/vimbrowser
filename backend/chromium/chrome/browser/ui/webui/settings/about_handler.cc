// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/about_handler.h"

#include <stddef.h>

#include <limits>
#include <string>
#include <string_view>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/metrics/user_metrics.h"
#include "base/metrics/user_metrics_action.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/time/default_clock.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_content_browser_client.h"
#include "chrome/browser/enterprise/browser_management/management_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/webui/theme_source.h"
#include "chrome/browser/upgrade_detector/upgrade_detector.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/grit/generated_resources.h"
#include "components/google/core/common/google_util.h"
#include "components/policy/core/common/management/management_service.h"
#include "components/policy/core/common/policy_namespace.h"
#include "components/policy/policy_constants.h"
#include "components/services/app_service/public/cpp/app_launch_util.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/l10n/l10n_util.h"
#include "v8/include/v8-version-string.h"


namespace {


std::string UpdateStatusToString(VersionUpdater::Status status) {
  std::string status_str;
  switch (status) {
    case VersionUpdater::CHECKING:
      status_str = "checking";
      break;
    case VersionUpdater::UPDATING:
      status_str = "updating";
      break;
    case VersionUpdater::NEARLY_UPDATED:
      status_str = "nearly_updated";
      break;
    case VersionUpdater::UPDATED:
      status_str = "updated";
      break;
    case VersionUpdater::FAILED:
    case VersionUpdater::FAILED_OFFLINE:
    case VersionUpdater::FAILED_CONNECTION_TYPE_DISALLOWED:
      status_str = "failed";
      break;
    case VersionUpdater::FAILED_HTTP:
      status_str = "failed_http";
      break;
    case VersionUpdater::FAILED_DOWNLOAD:
      status_str = "failed_download";
      break;
    case VersionUpdater::DISABLED:
      status_str = "disabled";
      break;
    case VersionUpdater::DISABLED_BY_ADMIN:
      status_str = "disabled_by_admin";
      break;
    case VersionUpdater::UPDATE_TO_ROLLBACK_VERSION_DISALLOWED:
      status_str = "update_to_rollback_version_disallowed";
      break;
    case VersionUpdater::NEED_PERMISSION_TO_UPDATE:
      status_str = "need_permission_to_update";
      break;
    case VersionUpdater::DEFERRED:
      status_str = "deferred";
      break;
  }

  return status_str;
}

}  // namespace

namespace settings {

AboutHandler::AboutHandler(Profile* profile)
    : profile_(profile), clock_(base::DefaultClock::GetInstance()) {
  UpgradeDetector::GetInstance()->AddObserver(this);
}

AboutHandler::~AboutHandler() {
  UpgradeDetector::GetInstance()->RemoveObserver(this);
}

void AboutHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "aboutPageReady", base::BindRepeating(&AboutHandler::HandlePageReady,
                                            base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "refreshUpdateStatus",
      base::BindRepeating(&AboutHandler::HandleRefreshUpdateStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openFeedbackDialog",
      base::BindRepeating(&AboutHandler::HandleOpenFeedbackDialog,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openHelpPage", base::BindRepeating(&AboutHandler::HandleOpenHelpPage,
                                          base::Unretained(this)));
#if BUILDFLAG(IS_MAC)
  web_ui()->RegisterMessageCallback(
      "promoteUpdater", base::BindRepeating(&AboutHandler::PromoteUpdater,
                                            base::Unretained(this)));
#endif

  content::URLDataSource::Add(profile_,
                              std::make_unique<ThemeSource>(profile_));
}

void AboutHandler::OnJavascriptAllowed() {
  apply_changes_from_upgrade_observer_ = true;
  version_updater_ = VersionUpdater::Create(web_ui()->GetWebContents());
  policy_registrar_ = std::make_unique<policy::PolicyChangeRegistrar>(
      g_browser_process->policy_service(),
      policy::PolicyNamespace(policy::POLICY_DOMAIN_CHROME, std::string()));
// TODO(b/330932781): Investigate and fix mismatched BUILDFLAG and comment.
}

void AboutHandler::OnJavascriptDisallowed() {
  // Invalidate all existing WeakPtrs so that no stale callbacks occur.
  weak_factory_.InvalidateWeakPtrs();

  apply_changes_from_upgrade_observer_ = false;
  version_updater_.reset();
  policy_registrar_.reset();
}

void AboutHandler::OnUpgradeRecommended() {
  if (apply_changes_from_upgrade_observer_) {
    // A version update is installed and ready to go. Refresh the UI so the
    // correct state will be shown.
    RequestUpdate();
  }
}

void AboutHandler::OnDeviceAutoUpdatePolicyChanged(
    const base::Value* previous_policy,
    const base::Value* current_policy) {
  bool previous_auto_update_disabled = false;
  if (previous_policy) {
    CHECK(previous_policy->is_bool());
    previous_auto_update_disabled = previous_policy->GetBool();
  }

  bool current_auto_update_disabled = false;
  if (current_policy) {
    CHECK(current_policy->is_bool());
    current_auto_update_disabled = current_policy->GetBool();
  }

  if (current_auto_update_disabled != previous_auto_update_disabled) {
    // Refresh the update status to refresh the status of the UI.
    RefreshUpdateStatus();
  }
}

void AboutHandler::HandlePageReady(const base::ListValue& args) {
  AllowJavascript();
}

void AboutHandler::HandleRefreshUpdateStatus(const base::ListValue& args) {
  AllowJavascript();
  RefreshUpdateStatus();
}

void AboutHandler::RefreshUpdateStatus() {
// On Chrome OS, do not check for an update automatically.
  RequestUpdate();
}

#if BUILDFLAG(IS_MAC)
void AboutHandler::PromoteUpdater(const base::ListValue& args) {
  version_updater_->PromoteUpdater();
}
#endif

void AboutHandler::HandleOpenFeedbackDialog(const base::ListValue& args) {
  DCHECK(args.empty());
  Browser* browser = chrome::FindBrowserWithTab(web_ui()->GetWebContents());
  chrome::OpenFeedbackDialog(browser,
                             feedback::kFeedbackSourceMdSettingsAboutPage);
}

void AboutHandler::HandleOpenHelpPage(const base::ListValue& args) {
  DCHECK(args.empty());
  Browser* browser = chrome::FindBrowserWithTab(web_ui()->GetWebContents());
  chrome::ShowHelp(browser, chrome::HelpSource::kWebUI);
}


void AboutHandler::RequestUpdate() {
  version_updater_->CheckForUpdate(
      base::BindRepeating(&AboutHandler::SetUpdateStatus,
                          weak_factory_.GetWeakPtr()),
#if BUILDFLAG(IS_MAC)
      base::BindRepeating(&AboutHandler::SetPromotionState,
                          weak_factory_.GetWeakPtr()));
#else
      VersionUpdater::PromoteCallback());
#endif  // BUILDFLAG(IS_MAC)
}

void AboutHandler::SetUpdateStatus(VersionUpdater::Status status,
                                   int progress,
                                   bool rollback,
                                   bool powerwash,
                                   const std::string& version,
                                   int64_t size,
                                   const std::u16string& message) {
  // Only UPDATING state should have progress set.
  DCHECK(status == VersionUpdater::UPDATING || progress == 0);

  base::DictValue event;
  event.Set("status", UpdateStatusToString(status));
  event.Set("message", message);
  event.Set("progress", progress);
  event.Set("rollback", rollback);
  event.Set("powerwash", powerwash);
  event.Set("version", version);
  // `base::DictValue` does not support int64_t, so convert to string.
  event.Set("size", base::NumberToString(size));

  FireWebUIListener("update-status-changed", event);
}

#if BUILDFLAG(IS_MAC)
void AboutHandler::SetPromotionState(VersionUpdater::PromotionState state) {
  // Worth noting: PROMOTE_DISABLED indicates that promotion is possible,
  // there's just something else going on right now (e.g. checking for update).
  bool hidden = state == VersionUpdater::PROMOTE_HIDDEN;
  bool disabled = state == VersionUpdater::PROMOTE_HIDDEN ||
                  state == VersionUpdater::PROMOTE_DISABLED ||
                  state == VersionUpdater::PROMOTED;
  bool actionable = state == VersionUpdater::PROMOTE_DISABLED ||
                    state == VersionUpdater::PROMOTE_ENABLED;

  std::u16string text;
  if (actionable) {
    text = l10n_util::GetStringUTF16(IDS_ABOUT_CHROME_AUTOUPDATE_ALL);
  } else if (state == VersionUpdater::PROMOTED) {
    text = l10n_util::GetStringUTF16(IDS_ABOUT_CHROME_AUTOUPDATE_ALL_IS_ON);
  }

  base::DictValue promo_state;
  promo_state.Set("hidden", hidden);
  promo_state.Set("disabled", disabled);
  promo_state.Set("actionable", actionable);
  if (!text.empty()) {
    promo_state.Set("text", text);
  }

  FireWebUIListener("promotion-state-changed", promo_state);
}
#endif  // BUILDFLAG(IS_MAC)


}  // namespace settings
