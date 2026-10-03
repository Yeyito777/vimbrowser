// Copyright 2016 The Chromium Embedded Framework Authors. Portions copyright
// 2016 The Chromium Authors. All rights reserved. Use of this source code is
// governed by a BSD-style license that can be found in the LICENSE file.

#include "cef/libcef/common/crash_reporter_client.h"

#include <string_view>
#include <utility>


#include "base/environment.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/crash/core/common/crash_key.h"
#include "content/public/common/content_switches.h"
#include "third_party/crashpad/crashpad/client/annotation.h"

#if BUILDFLAG(IS_MAC)
#include "cef/libcef/common/util_mac.h"
#endif

// Don't use CommandLine, FilePath or PathService on Windows. FilePath has
// dependencies outside of kernel32, which is disallowed by chrome_elf.
// CommandLine and PathService depend on global state that will not be
// initialized at the time the CefCrashReporterClient object is created.
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "chrome/common/chrome_paths.h"

#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_MAC)
#include "cef/libcef/common/cef_crash_report_utils.h"
#include "content/public/common/content_switches.h"
#endif


namespace {

using PathString = std::string;

PathString GetCrashConfigPath() {
  base::FilePath config_path;

#if BUILDFLAG(IS_MAC)
  // Start with the path to the main app Resources directory. May be empty if
  // not running in an app bundle.
  config_path = util_mac::GetMainResourcesDirectory();
#endif

  if (config_path.empty()) {
    // Start with the path to the running executable.
    if (!base::PathService::Get(base::DIR_EXE, &config_path)) {
      return PathString();
    }
  }

  return config_path.Append(FILE_PATH_LITERAL("crash_reporter.cfg")).value();
}


const char kKeyMapDelim = ',';

std::string NormalizeCrashKey(const std::string_view& key) {
  std::string str(key);
  std::replace(str.begin(), str.end(), kKeyMapDelim, '-');
  if (str.length() > crashpad::Annotation::kNameMaxLength) {
    return str.substr(0, crashpad::Annotation::kNameMaxLength);
  }
  return str;
}

void ParseURL(const std::string& value, std::string* url) {
  if (value.starts_with("http://") || value.starts_with("https://")) {
    *url = value;
    if (url->rfind('/') <= 8) {
      // Make sure the URL includes a path component. Otherwise, crash
      // upload will fail on older Windows versions due to
      // https://crbug.com/826564.
      *url += "/";
    }
  }
}

bool ParseBool(const std::string& value) {
  return base::EqualsCaseInsensitiveASCII(value, "true") || value == "1";
}

int ParseZeroBasedInt(const std::string& value) {
  int int_val;
  if (base::StringToInt(value, &int_val) && int_val > 0) {
    return int_val;
  }
  return 0;
}

}  // namespace


CefCrashReporterClient::CefCrashReporterClient() = default;
CefCrashReporterClient::~CefCrashReporterClient() = default;

// Be aware that logging is not initialized at the time this method is called.
bool CefCrashReporterClient::ReadCrashConfigFile() {
  if (has_crash_config_file_) {
    return true;
  }

  PathString config_path = GetCrashConfigPath();
  if (config_path.empty()) {
    return false;
  }

  FILE* fp = fopen(config_path.c_str(), "r");
  if (!fp) {
    return false;
  }

  char line[1000];

  size_t small_index = 0;
  size_t medium_index = 0;
  size_t large_index = 0;
  std::string map_keys;

  enum section {
    kNoSection,
    kConfigSection,
    kCrashKeysSection,
  } current_section = kNoSection;

  while (fgets(line, sizeof(line) - 1, fp) != nullptr) {
    std::string str = line;
    base::TrimString(str, base::kWhitespaceASCII, &str);
    if (str.empty() || str[0] == '#') {
      continue;
    }

    if (str == "[Config]") {
      current_section = kConfigSection;
      continue;
    } else if (str == "[CrashKeys]") {
      current_section = kCrashKeysSection;
      continue;
    } else if (str[0] == '[') {
      current_section = kNoSection;
      continue;
    }

    if (current_section == kNoSection) {
      continue;
    }

    size_t div = str.find('=');
    if (div == std::string::npos) {
      continue;
    }

    std::string name_str = str.substr(0, div);
    base::TrimString(name_str, base::kWhitespaceASCII, &name_str);
    std::string val_str = str.substr(div + 1);
    base::TrimString(val_str, base::kWhitespaceASCII, &val_str);
    if (name_str.empty()) {
      continue;
    }

    if (current_section == kConfigSection) {
      if (name_str == "ServerURL") {
        ParseURL(val_str, &server_url_);
      } else if (name_str == "ProductName") {
        product_name_ = val_str;
      } else if (name_str == "ProductVersion") {
        product_version_ = val_str;
      } else if (name_str == "RateLimitEnabled") {
        rate_limit_ = ParseBool(val_str);
      } else if (name_str == "MaxUploadsPerDay") {
        max_uploads_ = ParseZeroBasedInt(val_str);
      } else if (name_str == "MaxDatabaseSizeInMb") {
        max_db_size_ = ParseZeroBasedInt(val_str);
      } else if (name_str == "MaxDatabaseAgeInDays") {
        max_db_age_ = ParseZeroBasedInt(val_str);
      }
#if BUILDFLAG(IS_MAC)
      else if (name_str == "BrowserCrashForwardingEnabled") {
        enable_browser_crash_forwarding_ = ParseBool(val_str);
      }
#endif
    } else if (current_section == kCrashKeysSection) {
      // Skip duplicate definitions.
      if (!crash_keys_.empty() && crash_keys_.contains(name_str)) {
        continue;
      }

      KeySize size;
      size_t index;
      char group;
      if (val_str == "small") {
        size = SMALL_SIZE;
        index = small_index++;
        group = 'S';
      } else if (val_str == "medium") {
        size = MEDIUM_SIZE;
        index = medium_index++;
        group = 'M';
      } else if (val_str == "large") {
        size = LARGE_SIZE;
        index = large_index++;
        group = 'L';
      } else {
        continue;
      }

      name_str = NormalizeCrashKey(name_str);
      crash_keys_.insert(std::make_pair(name_str, std::make_pair(size, index)));

      const std::string& key =
          std::string(1, group) + "-" + std::string(1, 'A' + index);
      if (!map_keys.empty()) {
        map_keys.append(std::string(1, kKeyMapDelim));
      }
      map_keys.append(key + "=" + name_str);
    }
  }

  fclose(fp);

  if (!map_keys.empty()) {
    // Split |map_keys| across multiple Annotations if necessary.
    // Must match the logic in crash_report_utils::FilterParameters.
    using IDKey =
        crash_reporter::CrashKeyString<crashpad::Annotation::kValueMaxSize>;
    static IDKey ids[] = {
        {"K-A", IDKey::Tag::kArray},
        {"K-B", IDKey::Tag::kArray},
        {"K-C", IDKey::Tag::kArray},
    };

    // Make sure we can fit all possible name/value pairs.
    static_assert(std::size(ids) * crashpad::Annotation::kValueMaxSize >=
                      3 * 26 /* sizes (small, medium, large) * slots (A to Z) */
                          * (3 + 2 /* key size ("S-A") + delim size ("=,") */
                             + crashpad::Annotation::kNameMaxLength),
                  "Not enough storage for key map");

    size_t offset = 0;
    for (auto& id : ids) {
      size_t length = std::min(map_keys.size() - offset,
                               crashpad::Annotation::kValueMaxSize);
      id.Set(std::string_view(map_keys.data() + offset, length));
      offset += length;
      if (offset >= map_keys.size()) {
        break;
      }
    }
  }

  // Allow override of some values via environment variables.
  {
    std::unique_ptr<base::Environment> env(base::Environment::Create());
    if (const auto& var_str = env->GetVar("CEF_CRASH_REPORTER_SERVER_URL")) {
      ParseURL(*var_str, &server_url_);
    }
    if (const auto& var_str =
            env->GetVar("CEF_CRASH_REPORTER_RATE_LIMIT_ENABLED")) {
      rate_limit_ = ParseBool(*var_str);
    }
  }

  has_crash_config_file_ = true;
  return true;
}

bool CefCrashReporterClient::HasCrashConfigFile() const {
  return has_crash_config_file_;
}


void CefCrashReporterClient::GetProductInfo(ProductInfo* product_info) {
  product_info->product_name = product_name_;
  product_info->version = product_version_;
}

bool CefCrashReporterClient::GetCrashDumpLocation(base::FilePath* crash_dir) {
  // By setting the BREAKPAD_DUMP_LOCATION environment variable, an alternate
  // location to write breakpad crash dumps can be set.
  std::unique_ptr<base::Environment> env(base::Environment::Create());
  if (const auto& val = env->GetVar("BREAKPAD_DUMP_LOCATION")) {
    base::PathService::Override(chrome::DIR_CRASH_DUMPS,
                                base::FilePath::FromUTF8Unsafe(*val));
  }
  return base::PathService::Get(chrome::DIR_CRASH_DUMPS, crash_dir);
}


bool CefCrashReporterClient::GetCollectStatsConsent() {
  return true;
}

bool CefCrashReporterClient::GetCollectStatsInSample() {
  return true;
}

bool CefCrashReporterClient::ReportingIsEnforcedByPolicy(
    bool* crashpad_enabled) {
  *crashpad_enabled = true;
  return true;
}

std::string CefCrashReporterClient::GetUploadUrl() {
  return server_url_;
}

// See HandlerMain() in third_party/crashpad/crashpad/handler/handler_main.cc
// for supported arguments.
void CefCrashReporterClient::GetCrashOptionalArguments(
    std::vector<std::string>* arguments) {
  if (!rate_limit_) {
    arguments->emplace_back("--no-rate-limit");
  }

  if (max_uploads_ > 0) {
    arguments->push_back(std::string("--max-uploads=") +
                         base::NumberToString(max_uploads_));
  }

  if (max_db_size_ > 0) {
    arguments->push_back(std::string("--max-db-size=") +
                         base::NumberToString(max_db_size_));
  }

  if (max_db_age_ > 0) {
    arguments->push_back(std::string("--max-db-age=") +
                         base::NumberToString(max_db_age_));
  }
}


#if BUILDFLAG(IS_MAC)
bool CefCrashReporterClient::EnableBrowserCrashForwarding() {
  return enable_browser_crash_forwarding_;
}
#endif

// The new Crashpad Annotation API requires that annotations be declared using
// static storage. We work around this limitation by defining a fixed amount of
// storage for each key size and later substituting the actual key name during
// crash dump processing.

#define IDKEY(name) {name, IDKey::Tag::kArray}

#define IDKEY_ENTRIES(n)                                                     \
  IDKEY(n "-A"), IDKEY(n "-B"), IDKEY(n "-C"), IDKEY(n "-D"), IDKEY(n "-E"), \
      IDKEY(n "-F"), IDKEY(n "-G"), IDKEY(n "-H"), IDKEY(n "-I"),            \
      IDKEY(n "-J"), IDKEY(n "-K"), IDKEY(n "-L"), IDKEY(n "-M"),            \
      IDKEY(n "-N"), IDKEY(n "-O"), IDKEY(n "-P"), IDKEY(n "-Q"),            \
      IDKEY(n "-R"), IDKEY(n "-S"), IDKEY(n "-T"), IDKEY(n "-U"),            \
      IDKEY(n "-V"), IDKEY(n "-W"), IDKEY(n "-X"), IDKEY(n "-Y"),            \
      IDKEY(n "-Z")

#define IDKEY_FUNCTION(name, size_)                                         \
  static_assert(size_ <= crashpad::Annotation::kValueMaxSize,               \
                "Annotation size is too large.");                           \
  bool Set##name##Annotation(size_t index, const std::string_view& value) { \
    using IDKey = crash_reporter::CrashKeyString<size_>;                    \
    static IDKey ids[] = {IDKEY_ENTRIES(#name)};                            \
    if (index < std::size(ids)) {                                           \
      if (value.empty()) {                                                  \
        ids[index].Clear();                                                 \
      } else {                                                              \
        ids[index].Set(value);                                              \
      }                                                                     \
      return true;                                                          \
    }                                                                       \
    return false;                                                           \
  }

// The first argument must be kept synchronized with the logic in
// CefCrashReporterClient::ReadCrashConfigFile and
// crash_report_utils::FilterParameters.
IDKEY_FUNCTION(S, 64)
IDKEY_FUNCTION(M, 256)
IDKEY_FUNCTION(L, 1024)

bool CefCrashReporterClient::SetCrashKeyValue(const std::string_view& key,
                                              const std::string_view& value) {
  if (key.empty() || crash_keys_.empty()) {
    return false;
  }

  KeyMap::const_iterator it = crash_keys_.find(NormalizeCrashKey(key));
  if (it == crash_keys_.end()) {
    return false;
  }

  const KeySize size = it->second.first;
  const size_t index = it->second.second;

  base::AutoLock lock_scope(crash_key_lock_);

  switch (size) {
    case SMALL_SIZE:
      return SetSAnnotation(index, value);
    case MEDIUM_SIZE:
      return SetMAnnotation(index, value);
    case LARGE_SIZE:
      return SetLAnnotation(index, value);
  }

  return false;
}
