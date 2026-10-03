// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/updater/updater_scope.h"

#include <optional>

#include "base/command_line.h"
#include "base/path_service.h"
#include "build/build_config.h"
#include "chrome/updater/constants.h"
#include "chrome/updater/util/util.h"


namespace updater {
namespace {

bool IsSystemProcessForCommandLine(const base::CommandLine& command_line) {
  return command_line.HasSwitch(kSystemSwitch);
}

}  // namespace

std::optional<tagging::NeedsAdmin> NeedsAdminFromTagArgs(
    const std::optional<tagging::TagArgs> tag_args) {
  if (!tag_args) {
    return {};
  }
  if (!tag_args->apps.empty()) {
    return tag_args->apps.front().needs_admin;
  }
  if (tag_args->runtime_mode) {
    return tag_args->runtime_mode->needs_admin;
  }
  return {};
}

bool IsPrefersForCommandLine(const base::CommandLine& command_line) {
  return false;
}

UpdaterScope GetUpdaterScopeForCommandLine(
    const base::CommandLine& command_line) {
  return IsSystemProcessForCommandLine(command_line) ? UpdaterScope::kSystem
                                                     : UpdaterScope::kUser;
}

UpdaterScope GetUpdaterScope() {
  return GetUpdaterScopeForCommandLine(*base::CommandLine::ForCurrentProcess());
}

bool IsSystemInstall() {
  return IsSystemInstall(GetUpdaterScope());
}

bool IsSystemInstall(UpdaterScope scope) {
  return scope == UpdaterScope::kSystem;
}

}  // namespace updater
