// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_LANGUAGE_CORE_BROWSER_PREF_NAMES_H_
#define COMPONENTS_LANGUAGE_CORE_BROWSER_PREF_NAMES_H_

#include "build/build_config.h"

namespace language::prefs {

// The value to use for Accept-Languages HTTP header when making an HTTP
// request. This should not be set directly as it is a combination of
// kSelectedLanguages and kForcedLanguages. To update the list of preferred
// languages, set kSelectedLanguages and this pref will update automatically.
inline constexpr char kAcceptLanguages[] = "intl.accept_languages";

// List which contains the user-selected languages.
inline constexpr char kSelectedLanguages[] = "intl.selected_languages";

// List which contains the policy-forced languages.
inline constexpr char kForcedLanguages[] = "intl.forced_languages";


// The application locale as selected by the user, such as "en-AU". This may not
// necessarily be a string locale (a locale that we have strings for on this
// platform). Use |l10n_util::CheckAndResolveLocale| to convert it to a string
// locale if needed, such as "en-GB".
inline constexpr char kApplicationLocale[] = "intl.app_locale";


}  // namespace language::prefs

#endif  // COMPONENTS_LANGUAGE_CORE_BROWSER_PREF_NAMES_H_
