// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/browser_lifetime_handler.h"

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "build/build_config.h"
#include "chrome/browser/enterprise/browser_management/management_service_factory.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/grit/branded_strings.h"
#include "components/policy/core/common/management/management_service.h"


#include "chrome/browser/ui/browser_finder.h"
#include "chrome/grit/generated_resources.h"
#include "ui/base/l10n/l10n_util.h"

namespace settings {

namespace {


}  // namespace

BrowserLifetimeHandler::BrowserLifetimeHandler() = default;

BrowserLifetimeHandler::~BrowserLifetimeHandler() = default;

void BrowserLifetimeHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "restart", base::BindRepeating(&BrowserLifetimeHandler::HandleRestart,
                                     base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "relaunch", base::BindRepeating(&BrowserLifetimeHandler::HandleRelaunch,
                                      base::Unretained(this)));

  web_ui()->RegisterMessageCallback(
      "shouldShowRelaunchConfirmationDialog",
      base::BindRepeating(
          &BrowserLifetimeHandler::HandleShouldShowRelaunchConfirmationDialog,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getRelaunchConfirmationDialogDescription",
      base::BindRepeating(&BrowserLifetimeHandler::
                              HandleGetRelaunchConfirmationDialogDescription,
                          base::Unretained(this)));
}

void BrowserLifetimeHandler::HandleRestart(const base::ListValue& args) {
  chrome::AttemptRestart();
}

void BrowserLifetimeHandler::HandleRelaunch(const base::ListValue& args) {
  chrome::AttemptRelaunch();
}


void BrowserLifetimeHandler::HandleGetRelaunchConfirmationDialogDescription(
    const base::ListValue& args) {
  AllowJavascript();
  CHECK_EQ(2U, args.size());
  const base::Value& callback_id = args[0];
  CHECK(args[1].is_bool());
  const bool is_version_update = args[1].GetBool();

  size_t incognito_count = chrome::GetIncognitoBrowserCount();
  base::Value description;

  // The caller can specify if this is a confirmation dialog for browser version
  // update relaunch.
  if (is_version_update) {
    // The dialog description informs about a browser update after relaunch and
    // warns about incognito windows closure if any is open.
    description = base::Value(l10n_util::GetPluralStringFUTF16(
        IDS_UPDATE_RECOMMENDED, incognito_count));
  } else if (incognito_count > 0) {
    // The dialog description warns about incognito windows being closed after
    // relaunch.
    description = base::Value(l10n_util::GetPluralStringFUTF16(
        IDS_RELAUNCH_CONFIRMATION_DIALOG_BODY, incognito_count));
  }

  ResolveJavascriptCallback(callback_id, description);
}

void BrowserLifetimeHandler::HandleShouldShowRelaunchConfirmationDialog(
    const base::ListValue& args) {
  AllowJavascript();
  CHECK_EQ(2U, args.size());
  const base::Value& callback_id = args[0];
  CHECK(args[1].is_bool());
  const bool alwaysShowDialog = args[1].GetBool();

  // The caller can specify if the dialog should always be shown for a given
  // case by passing alwaysShowDialog parameter.
  if (alwaysShowDialog) {
    // Always show a confirmation dialog before the restart.
    ResolveJavascriptCallback(callback_id, true);
  } else {
    // Show a confirmation dialog before the restart if there is an incognito
    // window open.
    base::Value result = base::Value(chrome::GetIncognitoBrowserCount() > 0);
    ResolveJavascriptCallback(callback_id, result);
  }
}

}  // namespace settings
