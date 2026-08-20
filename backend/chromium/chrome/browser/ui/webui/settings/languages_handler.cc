// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/languages_handler.h"

#include "base/functional/bind.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/pref_names.h"
#include "components/language/core/browser/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"


namespace settings {

LanguagesHandler::LanguagesHandler() = default;

LanguagesHandler::~LanguagesHandler() = default;

void LanguagesHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getProspectiveUILanguage",
      base::BindRepeating(&LanguagesHandler::HandleGetProspectiveUILanguage,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setProspectiveUILanguage",
      base::BindRepeating(&LanguagesHandler::HandleSetProspectiveUILanguage,
                          base::Unretained(this)));
}

void LanguagesHandler::HandleGetProspectiveUILanguage(
    const base::ListValue& args) {
  const base::Value& callback_id = args[0];

  AllowJavascript();

  std::string locale;

  if (locale.empty()) {
    locale = g_browser_process->local_state()->GetString(
        language::prefs::kApplicationLocale);
  }

  ResolveJavascriptCallback(callback_id, base::Value(locale));
}

void LanguagesHandler::HandleSetProspectiveUILanguage(
    const base::ListValue& args) {
  AllowJavascript();
  CHECK_EQ(1U, args.size());

}

}  // namespace settings
