// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/preinstalled_web_app_config_utils.h"

#include <optional>

#include "base/auto_reset.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "build/build_config.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"


namespace web_app {

namespace {

std::optional<base::FilePath>&
GetPreinstalledWebAppConfigDirMutableForTesting() {
  static base::NoDestructor<std::optional<base::FilePath>>
      g_config_dir_for_testing(std::nullopt);
  return *g_config_dir_for_testing.get();
}


}  // namespace

namespace test {

std::optional<base::FilePath> GetPreinstalledWebAppConfigDirForTesting() {
  return GetPreinstalledWebAppConfigDirMutableForTesting();
}

base::AutoReset<std::optional<base::FilePath>>
SetPreinstalledWebAppConfigDirForTesting(const base::FilePath& config_dir) {
  return {&GetPreinstalledWebAppConfigDirMutableForTesting(),  // IN-TEST
                         config_dir};
}

}  // namespace test

base::FilePath GetPreinstalledWebAppConfigDirFromCommandLine(Profile* profile) {
  std::string command_line_directory =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kPreinstalledWebAppsDir);
  if (!command_line_directory.empty())
    return base::FilePath::FromUTF8Unsafe(command_line_directory);


  return {};
}

base::FilePath GetPreinstalledWebAppExtraConfigDirFromCommandLine(
    Profile* profile) {
  return base::FilePath();
}

base::FilePath GetPreinstalledWebAppConfigDir(Profile* profile) {
  return GetPreinstalledWebAppConfigDirFromCommandLine(profile);
}

base::FilePath GetPreinstalledWebAppExtraConfigDir(Profile* profile) {
  return GetPreinstalledWebAppExtraConfigDirFromCommandLine(profile);
}

}  // namespace web_app
