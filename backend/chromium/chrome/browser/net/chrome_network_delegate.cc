// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/chrome_network_delegate.h"

#include "base/base_paths.h"
#include "base/path_service.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"

#if BUILDFLAG(IS_CHROMEOS)
#include "ash/constants/ambient_time_of_day_constants.h"
#include "base/system/sys_info.h"
#include "chrome/common/chrome_paths.h"
#endif


namespace {

bool g_access_to_all_files_enabled = false;

#if BUILDFLAG(IS_CHROMEOS)
// Returns true if |allowlist| contains |path| or a parent of |path|.
bool IsPathOnAllowlist(const base::FilePath& path,
                       const std::vector<base::FilePath>& allowlist) {
  for (const auto& allowlisted_path : allowlist) {
    // base::FilePath::operator== should probably handle trailing separators.
    if (allowlisted_path == path.StripTrailingSeparators() ||
        allowlisted_path.IsParent(path)) {
      return true;
    }
  }
  return false;
}
#endif

#if BUILDFLAG(IS_CHROMEOS)
// Returns true if access is allowed for |path| for a user with |profile_path).
bool IsAccessAllowedChromeOS(const base::FilePath& path,
                             const base::FilePath& profile_path) {
  // Allow access to DriveFS logs. These reside in
  // $PROFILE_PATH/GCache/v2/<opaque id>/Logs.
  base::FilePath path_within_gcache_v2;
  if (profile_path.Append("GCache/v2")
          .AppendRelativePath(path, &path_within_gcache_v2)) {
    std::vector<std::string> components = path_within_gcache_v2.GetComponents();
    if (components.size() > 1 && components[1] == "Logs") {
      return true;
    }
  }

  // Use an allowlist to only allow access to files residing in the list of
  // directories below.
  static const base::FilePath::CharType* const kLocalAccessAllowList[] = {
      "/home/chronos/user/MyFiles",
      "/home/chronos/user/WebRTC Logs",
      "/home/chronos/user/google-assistant-library/log",
      "/home/chronos/user/log",
      "/home/chronos/user/crostini.icons",
      "/media",
      "/opt/oem",
      "/run/arc/sdcard/write/emulated/0",
      "/usr/share/chromeos-assets",
      "/var/log",
  };
  std::vector<base::FilePath> allowlist;
  for (const auto* allowlisted_path : kLocalAccessAllowList)
    allowlist.emplace_back(allowlisted_path);

  base::FilePath temp_dir;
  if (base::PathService::Get(base::DIR_TEMP, &temp_dir))
    allowlist.push_back(temp_dir);

  // The actual location of "/home/chronos/user/Xyz" is the Xyz directory under
  // the profile path ("/home/chronos/user' is a hard link to current primary
  // logged in profile.) For the support of multi-profile sessions, we are
  // switching to use explicit "$PROFILE_PATH/Xyz" path and here allow such
  // access.
  if (!profile_path.empty()) {
    allowlist.push_back(profile_path.AppendASCII("MyFiles"));
    const base::FilePath webrtc_logs = profile_path.AppendASCII("WebRTC Logs");
    allowlist.push_back(webrtc_logs);
  }
  // For developers using the linux-chromeos emulator, the MyFiles dir is at
  // $HOME/Downloads. Ensure developers can access it for manual testing.
  if (!base::SysInfo::IsRunningOnChromeOS()) {
    base::FilePath downloads_dir;
    if (base::PathService::Get(chrome::DIR_DEFAULT_DOWNLOADS, &downloads_dir))
      allowlist.push_back(downloads_dir);
  }
  // /run/imageloader is the root directory for all DLC packages. The "timeofday" package
  // specifically contains assets required for one of ash's screen saver themes.
  allowlist.push_back(
      base::FilePath("/run/imageloader").Append(ash::kTimeOfDayDlcId));

  return IsPathOnAllowlist(path, allowlist);
}
#endif  // BUILDFLAG(IS_CHROMEOS)


bool IsAccessAllowedInternal(const base::FilePath& path,
                             const base::FilePath& profile_path) {
  if (g_access_to_all_files_enabled)
    return true;

#if BUILDFLAG(IS_CHROMEOS)
  return IsAccessAllowedChromeOS(path, profile_path);
#else
  return true;
#endif
}

}  // namespace

// static
bool ChromeNetworkDelegate::IsAccessAllowed(
    const base::FilePath& path,
    const base::FilePath& profile_path) {
  return IsAccessAllowedInternal(path, profile_path);
}

// static
bool ChromeNetworkDelegate::IsAccessAllowed(
    const base::FilePath& path,
    const base::FilePath& absolute_path,
    const base::FilePath& profile_path) {
  return (IsAccessAllowedInternal(path, profile_path) &&
          IsAccessAllowedInternal(absolute_path, profile_path));
}

// static
void ChromeNetworkDelegate::EnableAccessToAllFilesForTesting(bool enabled) {
  g_access_to_all_files_enabled = enabled;
}
