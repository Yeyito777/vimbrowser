// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/country_codes/country_codes.h"

#include "build/build_config.h"

#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE)
#include <locale.h>
#endif

#if BUILDFLAG(IS_APPLE)
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "base/strings/string_util.h"

#if BUILDFLAG(IS_APPLE)
#include "base/apple/scoped_cftyperef.h"
#endif


namespace country_codes {

namespace {


}  // namespace

#if BUILDFLAG(IS_APPLE)

CountryId GetCurrentCountryID() {
  base::apple::ScopedCFTypeRef<CFLocaleRef> locale(CFLocaleCopyCurrent());
  CFStringRef country =
      (CFStringRef)CFLocaleGetValue(locale.get(), kCFLocaleCountryCode);
  if (!country) {
    return CountryId();
  }

  UniChar isobuf[2];
  CFRange char_range = CFRangeMake(0, 2);
  CFStringGetCharacters(country, char_range, isobuf);

  char code[2]{static_cast<char>(isobuf[0]), static_cast<char>(isobuf[1])};

  return CountryId(std::string_view(code, std::size(code)));
}

#elif BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)

CountryId GetCurrentCountryID() {
  const char* locale = setlocale(LC_MESSAGES, nullptr);
  if (!locale) {
    return CountryId();
  }

  // The format of a locale name is:
  // language[_territory][.codeset][@modifier], where territory is an ISO 3166
  // country code, which is what we want.

  // First remove the language portion.
  std::string locale_str(locale);
  size_t territory_delim = locale_str.find('_');
  if (territory_delim == std::string::npos) {
    return CountryId();
  }
  locale_str.erase(0, territory_delim + 1);

  // Next remove any codeset/modifier portion and uppercase.
  return CountryId(
      base::ToUpperASCII(locale_str.substr(0, locale_str.find_first_of(".@"))));
}

#endif  // OS_*
}  // namespace country_codes
