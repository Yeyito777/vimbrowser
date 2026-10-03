// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/core/common/cloud/cloud_policy_util.h"

#include "base/strings/strcat.h"
#include "base/task/single_thread_task_runner.h"
#include "base/version_info/version_info.h"
#include "build/build_config.h"
#include "components/policy/core/common/cloud/cloud_policy_constants.h"
#include "components/policy/proto/device_management_backend.pb.h"
#include "net/base/network_interfaces.h"


#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_FUCHSIA)
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#if BUILDFLAG(IS_APPLE)
#include <stddef.h>
#include <sys/sysctl.h>
#endif

#if BUILDFLAG(IS_MAC)
#import <SystemConfiguration/SCDynamicStoreCopySpecific.h>
#endif

#if BUILDFLAG(IS_LINUX)
#include <limits.h>  // For HOST_NAME_MAX
#endif

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/notreached.h"
#include "base/system/sys_info.h"
#include "components/version_info/version_info.h"



#if BUILDFLAG(IS_MAC)
#include "base/system/sys_info.h"
#endif

#if BUILDFLAG(IS_APPLE)
#include "base/apple/scoped_cftyperef.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#endif


namespace policy {

namespace em = enterprise_management;

namespace {

const int kMinimumVersionForExtensionInstallPolicy = 146;

}  // namespace

std::string GetMachineName() {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_FUCHSIA)
  char hostname[HOST_NAME_MAX];
  if (gethostname(hostname, HOST_NAME_MAX) == 0)  // Success.
    return hostname;
  return std::string();
#elif BUILDFLAG(IS_MAC)
  // Do not use NSHost currentHost, as it's very slow. http://crbug.com/138570
  SCDynamicStoreContext context = {0, NULL, NULL, NULL};
  base::apple::ScopedCFTypeRef<SCDynamicStoreRef> store(SCDynamicStoreCreate(
      kCFAllocatorDefault, CFSTR("chrome_sync"), NULL, &context));
  base::apple::ScopedCFTypeRef<CFStringRef> machine_name(
      SCDynamicStoreCopyLocalHostName(store.get()));
  if (machine_name.get())
    return base::SysCFStringRefToUTF8(machine_name.get());

  // Fall back to get computer name.
  base::apple::ScopedCFTypeRef<CFStringRef> computer_name(
      SCDynamicStoreCopyComputerName(store.get(), NULL));
  if (computer_name.get())
    return base::SysCFStringRefToUTF8(computer_name.get());

  // If all else fails, return to using a slightly nicer version of the hardware
  // model. Warning: This will soon return just a useless "Mac" string.
  std::string model = base::SysInfo::HardwareModelName();
  std::optional<base::SysInfo::HardwareModelNameSplit> split =
      base::SysInfo::SplitHardwareModelNameDoNotUse(model);

  if (!split) {
    return model;
  }

  return split.value().category;
#else
#error Unsupported platform
#endif
}

std::string GetOSVersion() {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_APPLE) || \
    BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA)
  return base::SysInfo::OperatingSystemVersion();
#else
  NOTREACHED();
#endif
}

std::string GetOSPlatform() {
  return std::string(version_info::GetOSType());
}

std::string GetOSArchitecture() {
  return base::SysInfo::OperatingSystemArchitecture();
}

std::string GetOSUsername() {
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_APPLE)
  struct passwd* creds = getpwuid(getuid());
  if (!creds || !creds->pw_name)
    return std::string();

  return creds->pw_name;
#elif BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FUCHSIA)
  // TODO(crbug.com/40200780): This should be fully implemented when there is
  // support in fuchsia.
  return std::string();
#else
  NOTREACHED();
#endif
}

em::Channel ConvertToProtoChannel(version_info::Channel channel) {
  switch (channel) {
    case version_info::Channel::UNKNOWN:
      return em::CHANNEL_UNKNOWN;
    case version_info::Channel::CANARY:
      return em::CHANNEL_CANARY;
    case version_info::Channel::DEV:
      return em::CHANNEL_DEV;
    case version_info::Channel::BETA:
      return em::CHANNEL_BETA;
    case version_info::Channel::STABLE:
      return em::CHANNEL_STABLE;
  }
}

std::string GetDeviceName() {
  return GetMachineName();
}

std::unique_ptr<em::BrowserDeviceIdentifier> GetBrowserDeviceIdentifier() {
  std::unique_ptr<em::BrowserDeviceIdentifier> device_identifier =
      std::make_unique<em::BrowserDeviceIdentifier>();
  device_identifier->set_computer_name(GetMachineName());
  device_identifier->set_serial_number("");
  return device_identifier;
}

std::string GetDeviceFqdn() {
  // Retrieves the FQDN of the computer for Windows and if this fails it reverts
  // to the hostname as known to the net subsystem.
  // TODO(crbug.com/398257759): Perform DNS lookup to obtain the FQDN for
  // non-Windows platforms.
  return net::GetHostName();
}

std::string GetNetworkName() {
  return net::GetWifiSSID();
}


bool IsMachineLevelUserCloudPolicyType(const std::string& type) {
  return type == dm_protocol::kChromeMachineLevelUserCloudPolicyType;
}

bool IsExtensionInstallPolicySupportedOnThisVersion() {
  return version_info::GetMajorVersionNumberAsInt() >=
         kMinimumVersionForExtensionInstallPolicy;
}

bool IsExtensionInstallPolicyType(const std::string& policy_type) {
  return policy_type ==
             dm_protocol::kChromeExtensionInstallUserCloudPolicyType ||
         policy_type ==
             dm_protocol::kChromeExtensionInstallMachineLevelCloudPolicyType;
}

bool IsChromePolicyType(const std::string& policy_type) {
  return policy_type == dm_protocol::GetChromeUserPolicyType() ||
         policy_type == dm_protocol::kChromeMachineLevelUserCloudPolicyType;
}

bool IsMachineLevelPolicyType(const std::string& policy_type) {
  return policy_type == dm_protocol::kChromeMachineLevelUserCloudPolicyType ||
         policy_type ==
             dm_protocol::kChromeExtensionInstallMachineLevelCloudPolicyType;
}

bool IsUserLevelPolicyType(const std::string& policy_type) {
  return policy_type == dm_protocol::GetChromeUserPolicyType() ||
         policy_type == dm_protocol::kChromeExtensionInstallUserCloudPolicyType;
}

std::string PolicyTypeLogPrefix(std::string_view policy_type,
                                std::string_view settings_entity_id) {
  if (settings_entity_id.empty()) {
    return base::StrCat({"[policy_type=", policy_type, "] "});
  }

  return base::StrCat({"[policy_type=", policy_type,
                       "settings_entity_id=", settings_entity_id, "] "});
}

}  // namespace policy
