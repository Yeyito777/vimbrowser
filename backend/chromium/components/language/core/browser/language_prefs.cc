// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/language/core/browser/language_prefs.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "build/build_config.h"
#include "components/language/core/browser/pref_names.h"
#include "components/language/core/common/language_util.h"
#include "components/language/core/common/locale_util.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/strings/grit/components_locale_settings.h"
#include "ui/base/l10n/l10n_util.h"

namespace language {

const char kFallbackInputMethodLocale[] = "en-US";

void LanguagePrefs::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterStringPref(language::prefs::kAcceptLanguages,
                               l10n_util::GetStringUTF8(IDS_ACCEPT_LANGUAGES),
                               user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);

  registry->RegisterStringPref(language::prefs::kSelectedLanguages,
                               std::string(),
                               user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);

  registry->RegisterListPref(language::prefs::kForcedLanguages);
}

LanguagePrefs::LanguagePrefs(PrefService* user_prefs) : prefs_(user_prefs) {
  InitializeSelectedLanguagesPref();
  UpdateAcceptLanguagesPref();
  base::RepeatingClosure callback = base::BindRepeating(
      &LanguagePrefs::UpdateAcceptLanguagesPref, base::Unretained(this));
  pref_change_registrar_.Init(prefs_);
  pref_change_registrar_.Add(language::prefs::kForcedLanguages, callback);
  pref_change_registrar_.Add(language::prefs::kSelectedLanguages, callback);
}

LanguagePrefs::~LanguagePrefs() {
  pref_change_registrar_.RemoveAll();
}

void LanguagePrefs::GetAcceptLanguagesList(
    std::vector<std::string>* languages) const {
  DCHECK(languages);
  DCHECK(languages->empty());
  const std::string& key = language::prefs::kAcceptLanguages;

  *languages = base::SplitString(prefs_->GetString(key), ",",
                                 base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
}

void LanguagePrefs::GetUserSelectedLanguagesList(
    std::vector<std::string>* languages) const {
  DCHECK(languages);
  DCHECK(languages->empty());
  const std::string& key = language::prefs::kSelectedLanguages;
  *languages = base::SplitString(prefs_->GetString(key), ",",
                                 base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
}

void LanguagePrefs::SetUserSelectedLanguagesList(
    const std::vector<std::string>& languages) {
  std::vector<std::string> filtered_languages =
      l10n_util::KeepAcceptedLanguages(languages);
  std::string languages_str = base::JoinString(filtered_languages, ",");
  prefs_->SetString(language::prefs::kSelectedLanguages, languages_str);
}

void LanguagePrefs::GetDeduplicatedUserLanguages(
    std::string* deduplicated_languages_string) {
  std::vector<std::string> deduplicated_languages;
  forced_languages_set_.clear();

  // Add policy languages.
  for (const auto& language :
       prefs_->GetList(language::prefs::kForcedLanguages)) {
    if (forced_languages_set_.find(language.GetString()) ==
        forced_languages_set_.end()) {
      deduplicated_languages.emplace_back(language.GetString());
      forced_languages_set_.insert(language.GetString());
    }
  }

  // Add non-duplicate user-selected languages.
  for (auto& language :
       base::SplitString(prefs_->GetString(language::prefs::kSelectedLanguages),
                         ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    if (forced_languages_set_.find(language) == forced_languages_set_.end())
      deduplicated_languages.emplace_back(std::move(language));
  }
  *deduplicated_languages_string =
      base::JoinString(deduplicated_languages, ",");
}

void LanguagePrefs::UpdateAcceptLanguagesPref() {
  std::string deduplicated_languages_string;
  GetDeduplicatedUserLanguages(&deduplicated_languages_string);
  if (deduplicated_languages_string !=
      prefs_->GetString(language::prefs::kAcceptLanguages))
    prefs_->SetString(language::prefs::kAcceptLanguages,
                      deduplicated_languages_string);
}


bool LanguagePrefs::IsForcedLanguage(const std::string& language) {
  return forced_languages_set_.find(language) != forced_languages_set_.end();
}

void LanguagePrefs::InitializeSelectedLanguagesPref() {
  // Initializes user-selected languages if they're empty.
  // This is important so that previously saved languages aren't overwritten.
  if (prefs_->GetString(language::prefs::kSelectedLanguages).empty()) {
    prefs_->SetString(language::prefs::kSelectedLanguages,
                      prefs_->GetString(language::prefs::kAcceptLanguages));
  }
}

void ResetLanguagePrefs(PrefService* prefs) {
  prefs->ClearPref(language::prefs::kSelectedLanguages);
  prefs->ClearPref(language::prefs::kAcceptLanguages);
}

std::string GetFirstLanguage(std::string_view language_list) {
  auto end = language_list.find(",");
  return std::string(language_list.substr(0, end));
}

}  // namespace language
