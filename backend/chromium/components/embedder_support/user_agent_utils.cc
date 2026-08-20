// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/embedder_support/user_agent_utils.h"

#include <stdint.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/system/sys_info.h"
#include "base/version.h"
#include "build/branding_buildflags.h"
#include "build/build_config.h"
#include "cef/libcef/features/features.h"
#include "components/embedder_support/pref_names.h"
#include "components/embedder_support/switches.h"
#include "components/policy/core/common/policy_pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/version_info/version_info.h"
#include "net/http/http_util.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/common/user_agent/user_agent_metadata.h"


#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"
#endif


#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_MAC)
#include <sys/utsname.h>
#endif

#if BUILDFLAG(ENABLE_CEF)
constexpr char kUserAgentProductAndVersion[] = "user-agent-product";
#endif

namespace embedder_support {

namespace {


const blink::UserAgentBrandList GetUserAgentBrandList(
    const std::string& major_version,
    const std::string& full_version,
    blink::UserAgentBrandVersionType output_version_type,
    std::optional<blink::UserAgentBrandVersion> additional_brand_version) {
  int major_version_number;
  bool parse_result = base::StringToInt(major_version, &major_version_number);
  DCHECK(parse_result);
  std::optional<std::string> brand;
#if !BUILDFLAG(CHROMIUM_BRANDING)
  brand = version_info::GetProductName();
#endif

  std::string brand_version =
      output_version_type == blink::UserAgentBrandVersionType::kFullVersion
          ? full_version
          : major_version;

  return GenerateBrandVersionList(major_version_number, brand, brand_version,
                                  output_version_type,
                                  additional_brand_version);
}

// Return UserAgentBrandList with the major version populated in the brand
// `version` value.
// TODO(crbug.com/1291612): Consolidate *MajorVersionList() methods by using
// GetVersionNumber()
const blink::UserAgentBrandList GetUserAgentBrandMajorVersionListInternal(
    std::optional<blink::UserAgentBrandVersion> additional_brand_version) {
  return GetUserAgentBrandList(version_info::GetMajorVersionNumber(),
                               std::string(version_info::GetVersionNumber()),
                               blink::UserAgentBrandVersionType::kMajorVersion,
                               additional_brand_version);
}

// For desktop and android:
// Returns true if kReduceUserAgentMinorVersionName is enabled.
//
// It helps us avoid introducing individual enterprise policy controls for
// sending unified platform for the user agent string.
bool ShouldSendUserAgentUnifiedPlatform() {
  return base::FeatureList::IsEnabled(
      blink::features::kReduceUserAgentMinorVersion);
}

// Return UserAgentBrandList with the full version populated in the brand
// `version` value.
// TODO(crbug.com/1291612): Consolidate *FullVersionList() methods by using
// GetVersionNumber()
const blink::UserAgentBrandList GetUserAgentBrandFullVersionListInternal(
    std::optional<blink::UserAgentBrandVersion> additional_brand_version) {
  return GetUserAgentBrandList(version_info::GetMajorVersionNumber(),
                               std::string(version_info::GetVersionNumber()),
                               blink::UserAgentBrandVersionType::kFullVersion,
                               additional_brand_version);
}

// Internal function to handle return the full or "reduced" user agent string,
// depending on the Reduce User-Agent reduction phase features.
std::string GetUserAgentInternal() {
  std::string product = GetProductAndVersion();
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(kHeadless)) {
    product.insert(0, "Headless");
  }


  return ShouldSendUserAgentUnifiedPlatform()
             ? BuildUnifiedPlatformUserAgentFromProduct(product)
             : BuildUserAgentFromProduct(product);
}

// Generate random order list based on the input size and seed.
// Manually implement a stable permutation shuffle since STL random number
// engines and generators are banned and helpers in base/rand_util.h not
// supported seed shuffle.
std::vector<size_t> GetRandomOrder(int seed, size_t size) {
  CHECK_GE(size, 2u);
  CHECK_LE(size, 4u);

  if (size == 2u) {
    return {seed % size, (seed + 1) % size};
  } else if (size == 3u) {
    // Pick a stable permutation seeded by major version number. any values here
    // and in order should be under three.
    static constexpr std::array<std::array<size_t, 3>, 6> orders{
        {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}}};
    const std::array<size_t, 3> order = orders[seed % orders.size()];
    return std::vector<size_t>(order.begin(), order.end());
  } else {
    // Pick a stable permutation seeded by major version number. any values
    // here and in order should be under four.
    static constexpr std::array<std::array<size_t, 4>, 24> orders{
        {{0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1}, {0, 3, 1, 2},
         {0, 3, 2, 1}, {1, 0, 2, 3}, {1, 0, 3, 2}, {1, 2, 0, 3}, {1, 2, 3, 0},
         {1, 3, 0, 2}, {1, 3, 2, 0}, {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3},
         {2, 1, 3, 0}, {2, 3, 0, 1}, {2, 3, 1, 0}, {3, 0, 1, 2}, {3, 0, 2, 1},
         {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0}}};
    const std::array<size_t, 4> order = orders[seed % orders.size()];
    return std::vector<size_t>(order.begin(), order.end());
  }
}

// Shuffle the generated brand version list based on the seed.
blink::UserAgentBrandList ShuffleBrandList(
    blink::UserAgentBrandList brand_version_list,
    int seed) {
  const std::vector<size_t> order =
      GetRandomOrder(seed, brand_version_list.size());
  CHECK_EQ(brand_version_list.size(), order.size());

  blink::UserAgentBrandList shuffled_brand_version_list(
      brand_version_list.size());
  for (size_t i = 0; i < order.size(); i++) {
    shuffled_brand_version_list[order[i]] = brand_version_list[i];
  }

  return shuffled_brand_version_list;
}

std::string GetUserAgentPlatform() {
#if BUILDFLAG(IS_MAC)
  return "Macintosh; ";
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  return "X11; ";  // strange, but that's what Firefox uses
#else
#error Unsupported platform
#endif
}

std::string GetUnifiedPlatform() {
#if BUILDFLAG(IS_LINUX)
  // This constant is only used on Android (desktop) and Linux.
  constexpr char kUnifiedPlatformLinuxX64[] = "X11; Linux x86_64";
#endif
#if BUILDFLAG(IS_MAC)
  return "Macintosh; Intel Mac OS X 10_15_7";
#elif BUILDFLAG(IS_LINUX)
  return kUnifiedPlatformLinuxX64;
#else
#error Unsupported platform
#endif
}

// Builds a string that describes the CPU type when available (or blank
// otherwise).
std::string BuildCpuInfo() {
  std::string cpuinfo;

#if BUILDFLAG(IS_MAC)
  cpuinfo = "Intel";
#elif BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_MAC)
  // Should work on any Posix system.
  struct utsname unixinfo;
  uname(&unixinfo);

  // special case for biarch systems
  if (UNSAFE_TODO(strcmp(unixinfo.machine, "x86_64")) == 0 &&
      sizeof(void*) == sizeof(int32_t)) {
    cpuinfo.assign("i686 (x86_64)");
  } else {
    cpuinfo.assign(unixinfo.machine);
  }
#endif

  return cpuinfo;
}

// Returns the OS version.
// On Android, the string will only include the build number and model if
// relevant enums indicate they should be included.
std::string GetOSVersion(IncludeAndroidBuildNumber include_android_build_number,
                         IncludeAndroidModel include_android_model) {
  std::string os_version;
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_CHROMEOS)
  int32_t os_major_version = 0;
  int32_t os_minor_version = 0;
  int32_t os_bugfix_version = 0;
  base::SysInfo::OperatingSystemVersionNumbers(
      &os_major_version, &os_minor_version, &os_bugfix_version);

#if BUILDFLAG(IS_MAC)
  // A significant amount of web content breaks if the reported "Mac
  // OS X" major version number is greater than 10. Continue to report
  // this as 10_15_7, the last dot release for that macOS version.
  if (os_major_version > 10) {
    os_major_version = 10;
    os_minor_version = 15;
    os_bugfix_version = 7;
  }
#endif

#endif


  base::StringAppendF(&os_version,
#if BUILDFLAG(IS_MAC)
                      "%d_%d_%d", os_major_version, os_minor_version,
                      os_bugfix_version
#else
                      ""
#endif
  );
  return os_version;
}

// Builds a User-agent compatible string that describes the OS and CPU type.
// On Android, the string will only include the build number and model if
// relevant enums indicate they should be included.
std::string BuildOSCpuInfo(
    IncludeAndroidBuildNumber include_android_build_number,
    IncludeAndroidModel include_android_model) {
  return BuildOSCpuInfoFromOSVersionAndCpuType(
      GetOSVersion(include_android_build_number, include_android_model),
      BuildCpuInfo());
}

}  // namespace

std::string GetProductAndVersion() {
#if BUILDFLAG(ENABLE_CEF)
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(kUserAgentProductAndVersion)) {
    return command_line->GetSwitchValueASCII(kUserAgentProductAndVersion);
  }
#endif

  return base::FeatureList::IsEnabled(
             blink::features::kReduceUserAgentMinorVersion)
             ? version_info::GetProductNameAndVersionForReducedUserAgent()
             : std::string(
                   version_info::GetProductNameAndVersionForUserAgent());
}

std::optional<std::string> GetUserAgentFromCommandLine() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(kUserAgent)) {
    std::string ua = command_line->GetSwitchValueASCII(kUserAgent);
    if (net::HttpUtil::IsValidHeaderValue(ua)) {
      return ua;
    }
    LOG(WARNING) << "Ignored invalid value for flag --" << kUserAgent;
  }
  return std::nullopt;
}

std::string GetUserAgent() {
  std::optional<std::string> custom_ua = GetUserAgentFromCommandLine();
  if (custom_ua.has_value()) {
    return custom_ua.value();
  }

  return GetUserAgentInternal();
}

const blink::UserAgentBrandList GetUserAgentBrandMajorVersionList(
    std::optional<blink::UserAgentBrandVersion> additional_brand_version) {
  return GetUserAgentBrandMajorVersionListInternal(additional_brand_version);
}

const blink::UserAgentBrandList GetUserAgentBrandFullVersionList(
    std::optional<blink::UserAgentBrandVersion> additional_brand_version) {
  return GetUserAgentBrandFullVersionListInternal(additional_brand_version);
}

// Generate a pseudo-random permutation of the following brand/version pairs:
//   1. The base project (i.e. Chromium)
//   2. The browser brand, if available
//   3. A randomized string containing GREASE characters to ensure proper
//      header parsing, along with an arbitrarily low version to ensure proper
//      version checking.
//   4. Additional brand/version pairs.
blink::UserAgentBrandList GenerateBrandVersionList(
    int seed,
    std::optional<std::string> brand,
    const std::string& version,
    blink::UserAgentBrandVersionType output_version_type,
    std::optional<blink::UserAgentBrandVersion> additional_brand_version) {
  DCHECK_GE(seed, 0);

  blink::UserAgentBrandVersion greasey_bv =
      GetGreasedUserAgentBrandVersion(seed, output_version_type);
  blink::UserAgentBrandVersion chromium_bv = {"Chromium", version};

  blink::UserAgentBrandList brand_version_list = {std::move(greasey_bv),
                                                  std::move(chromium_bv)};
  if (brand) {
    brand_version_list.emplace_back(brand.value(), version);
  }
  if (additional_brand_version) {
    brand_version_list.emplace_back(additional_brand_version.value());
  }

  return ShuffleBrandList(brand_version_list, seed);
}

// Process greased overridden brand version which is either major version or
// full version, return the corresponding output version type.
blink::UserAgentBrandVersion GetProcessedGreasedBrandVersion(
    const std::string& greasey_brand,
    const std::string& greasey_version,
    blink::UserAgentBrandVersionType output_version_type) {
  std::string greasey_major_version;
  std::string greasey_full_version;
  base::Version version(greasey_version);
  DCHECK(version.IsValid());

  // If the greased overridden version is a significant version type:
  // * Major version: set the major version as the overridden version
  // * Full version number: extending the version number with ".0.0.0"
  // If the overridden version is full version format:
  // * Major version: set the major version to match significant version format
  // * Full version: set the full version as the overridden version
  // https://wicg.github.io/ua-client-hints/#user-agent-full-version
  if (version.components().size() > 1) {
    greasey_major_version = base::NumberToString(version.components()[0]);
    greasey_full_version = greasey_version;
  } else {
    greasey_major_version = greasey_version;
    greasey_full_version = base::StrCat({greasey_version, ".0.0.0"});
  }

  blink::UserAgentBrandVersion output_greasey_bv = {
      greasey_brand,
      output_version_type == blink::UserAgentBrandVersionType::kFullVersion
          ? greasey_full_version
          : greasey_major_version};
  return output_greasey_bv;
}

blink::UserAgentBrandVersion GetGreasedUserAgentBrandVersion(
    int seed,
    blink::UserAgentBrandVersionType output_version_type) {
  std::string greasey_brand;
  std::string greasey_version;
  const std::vector<std::string> greasey_chars = {" ", "(", ":", "-", ".", "/",
                                                  ")", ";", "=", "?", "_"};
  const std::vector<std::string> greased_versions = {"8", "99", "24"};
  // See the spec:
  // https://wicg.github.io/ua-client-hints/#create-arbitrary-brands-section
  greasey_brand =
      base::StrCat({"Not", greasey_chars[(seed) % greasey_chars.size()], "A",
                    greasey_chars[(seed + 1) % greasey_chars.size()], "Brand"});
  greasey_version = greased_versions[seed % greased_versions.size()];
  return GetProcessedGreasedBrandVersion(greasey_brand, greasey_version,
                                         output_version_type);
}

bool GetMobileBitForUAMetadata() {
  // The mobile bit for UA-CH is true if the platform is iOS, or if it's
  // Android and not a desktop form factor, AND the kUseMobileUserAgent switch
  // is present.

  return false;
}

std::string GetPlatformVersion() {
#if BUILDFLAG(IS_LINUX)
  // TODO(crbug.com/40245146): Remove this Blink feature
  if (base::FeatureList::IsEnabled(
          blink::features::kReduceUserAgentDataLinuxPlatformVersion)) {
    return std::string();
  }
#endif



  int32_t major, minor, bugfix = 0;
  base::SysInfo::OperatingSystemVersionNumbers(&major, &minor, &bugfix);
  return base::StringPrintf("%d.%d.%d", major, minor, bugfix);
}

std::string GetPlatformForUAMetadata() {

#if BUILDFLAG(IS_MAC)
  // TODO(crbug.com/40704421): This can be removed/re-refactored once we use
  // "macOS" by default
  return "macOS";
#else
  return std::string(version_info::GetOSType());
#endif
}

blink::UserAgentMetadata GetUserAgentMetadata(bool only_low_entropy_ch) {
  blink::UserAgentMetadata metadata;

  // Low entropy client hints.
  metadata.brand_version_list =
      GetUserAgentBrandMajorVersionListInternal(std::nullopt);
  metadata.mobile = GetMobileBitForUAMetadata();
  metadata.platform = GetPlatformForUAMetadata();

  // For users providing a valid user-agent override via the command line:
  // If kUACHOverrideBlank is enabled, set user-agent metadata with the
  // default blank values, otherwise return the default UserAgentMetadata values
  // to populate and send only the low entropy client hints.
  // Notes: Sending low entropy hints with empty values may cause requests being
  // blocked by web application firewall software, etc.
  std::optional<std::string> custom_ua = GetUserAgentFromCommandLine();
  if (custom_ua.has_value()) {
    return base::FeatureList::IsEnabled(blink::features::kUACHOverrideBlank)
               ? blink::UserAgentMetadata()
               : metadata;
  }

  if (only_low_entropy_ch) {
    return metadata;
  }

  // High entropy client hints.
  metadata.brand_full_version_list =
      GetUserAgentBrandFullVersionListInternal(std::nullopt);
  metadata.full_version = std::string(version_info::GetVersionNumber());
  metadata.architecture = GetCpuArchitecture();
  metadata.model = BuildModelInfo();
  metadata.form_factors = GetFormFactorsClientHint(metadata, metadata.mobile);
  metadata.bitness = GetCpuBitness();
  metadata.wow64 = IsWoW64();
  metadata.platform_version = GetPlatformVersion();
  return metadata;
}

std::vector<std::string> GetFormFactorsClientHint(
    const blink::UserAgentMetadata& metadata,
    bool is_mobile) {
  // By default, use "Mobile" or "Desktop" depending on the `mobile` bit.
  std::vector<std::string> form_factors = {
      is_mobile ? blink::kMobileFormFactor : blink::kDesktopFormFactor};

  return form_factors;
}


std::string GetUnifiedPlatformForTesting() {
  return GetUnifiedPlatform();
}

// Return the CPU architecture in Windows/Mac/POSIX/Fuchsia and the empty string
// on Android or if unknown.
std::string GetCpuArchitecture() {
#if BUILDFLAG(IS_MAC)
  base::mac::CPUType cpu_type = base::mac::GetCPUType();
  if (cpu_type == base::mac::CPUType::kIntel) {
    return "x86";
  } else if (cpu_type == base::mac::CPUType::kArm ||
             cpu_type == base::mac::CPUType::kTranslatedIntel) {
    return "arm";
  }
#else
  std::string cpu_info = BuildCpuInfo();
  if (base::StartsWith(cpu_info, "arm") ||
      base::StartsWith(cpu_info, "aarch")) {
    return "arm";
  } else if ((base::StartsWith(cpu_info, "i") &&
              cpu_info.substr(2, 2) == "86") ||
             base::StartsWith(cpu_info, "x86")) {
    return "x86";
  }
#endif
  DLOG(WARNING) << "Unrecognized CPU Architecture";
  return std::string();
}

// Return the CPU bitness in Windows/Mac/POSIX/Fuchsia and the empty string
// on Android.
std::string GetCpuBitness() {
#if BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_FUCHSIA)
  return "64";
#else
  return BuildCpuInfo().contains("64") ? "64" : "32";
#endif
}

std::string BuildOSCpuInfoFromOSVersionAndCpuType(const std::string& os_version,
                                                  const std::string& cpu_type) {
  std::string os_cpu;

#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_APPLE)
  // Should work on any Posix system.
  struct utsname unixinfo;
  uname(&unixinfo);
#endif

  base::StringAppendF(&os_cpu,
#if BUILDFLAG(IS_MAC)
                      "%s Mac OS X %s", cpu_type.c_str(), os_version.c_str()
#else
                      "%s %s",
                      unixinfo.sysname,  // e.g. Linux
                      cpu_type.c_str()   // e.g. i686
#endif
  );

  return os_cpu;
}

std::string BuildUnifiedPlatformUserAgentFromProduct(
    const std::string& product) {
  return BuildUserAgentFromOSAndProduct(GetUnifiedPlatform(), product);
}

std::string BuildUserAgentFromProduct(const std::string& product) {
  std::string os_info;
  base::StringAppendF(&os_info, "%s%s", GetUserAgentPlatform().c_str(),
                      BuildOSCpuInfo(IncludeAndroidBuildNumber::Exclude,
                                     IncludeAndroidModel::Include)
                          .c_str());
  return BuildUserAgentFromOSAndProduct(os_info, product);
}

std::string BuildModelInfo() {

  return std::string();
}


std::string BuildUserAgentFromOSAndProduct(const std::string& os_info,
                                           const std::string& product) {
  // Derived from Safari's UA string.
  // This is done to expose our product name in a manner that is maximally
  // compatible with Safari, we hope!!
  std::string user_agent;
  base::StringAppendF(&user_agent,
                      "Mozilla/5.0 (%s) AppleWebKit/537.36 (KHTML, like Gecko) "
                      "%s Safari/537.36",
                      os_info.c_str(), product.c_str());
  return user_agent;
}

bool IsWoW64() {
  return false;
}

}  // namespace embedder_support
