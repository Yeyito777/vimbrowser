// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/apps/app_service/intent_util.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/compiler_specific.h"
#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/debug/dump_without_crashing.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/apps/app_service/file_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/extensions/application_launch.h"
#include "components/services/app_service/public/cpp/file_handler_info.h"
#include "components/services/app_service/public/cpp/intent_filter_util.h"
#include "components/services/app_service/public/cpp/intent_util.h"
#include "extensions/common/api/app_runtime.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handlers/file_handler_info.h"
#include "extensions/common/manifest_handlers/web_file_handlers_info.h"
#include "extensions/common/url_pattern.h"
#include "extensions/common/url_pattern_set.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"
#include "third_party/re2/src/re2/re2.h"
#include "url/gurl.h"
#include "url/url_constants.h"


namespace apps_util {

namespace {



}  // namespace

apps::IntentFilterPtr CreateFileFilter(
    const std::vector<std::string>& intent_actions,
    const std::vector<std::string>& mime_types,
    const std::vector<std::string>& file_extensions,
    const std::string& activity_name,
    bool include_directories) {
  DCHECK(!mime_types.empty() || !file_extensions.empty());
  auto intent_filter = std::make_unique<apps::IntentFilter>();

  // kAction == View, Share etc.
  apps::ConditionValues action_condition_values;
  for (auto& action : intent_actions) {
    action_condition_values.push_back(std::make_unique<apps::ConditionValue>(
        action, apps::PatternMatchType::kLiteral));
  }
  if (!action_condition_values.empty()) {
    intent_filter->conditions.push_back(std::make_unique<apps::Condition>(
        apps::ConditionType::kAction, std::move(action_condition_values)));
  }

  apps::ConditionValues file_condition_values;

  // Mime types.
  for (auto& mime_type : mime_types) {
    file_condition_values.push_back(std::make_unique<apps::ConditionValue>(
        mime_type, apps::PatternMatchType::kMimeType));
  }
  // And file extensions.
  for (const std::string& extension : file_extensions) {
    file_condition_values.push_back(std::make_unique<apps::ConditionValue>(
        extension, apps::PatternMatchType::kFileExtension));
  }
  if (include_directories) {
    file_condition_values.push_back(std::make_unique<apps::ConditionValue>(
        "", apps::PatternMatchType::kIsDirectory));
  }

  DCHECK(!file_condition_values.empty());
  if (!file_condition_values.empty()) {
    intent_filter->conditions.push_back(std::make_unique<apps::Condition>(
        apps::ConditionType::kFile, std::move(file_condition_values)));
  }

  if (!activity_name.empty()) {
    intent_filter->activity_name = activity_name;
  }

  return intent_filter;
}


apps::IntentFilters CreateIntentFiltersForChromeApp(
    const extensions::Extension* extension) {
  apps::IntentFilters filters;

  // Check that the extension can be launched with files. This includes all
  // platform apps and allowlisted extensions.
  if (!CanLaunchViaEvent(extension)) {
    return filters;
  }

  const extensions::FileHandlersInfo* file_handlers =
      extensions::FileHandlers::GetFileHandlers(extension);
  if (!file_handlers) {
    return filters;
  }

  for (const apps::FileHandlerInfo& handler : *file_handlers) {
    // "share_with", "add_to" and "pack_with" are ignored in the Files app
    // frontend.
    if (handler.verb != apps::file_handler_verbs::kOpenWith) {
      continue;
    }
    std::vector<std::string> mime_types(handler.types.begin(),
                                        handler.types.end());
    std::vector<std::string> file_extensions(handler.extensions.begin(),
                                             handler.extensions.end());
    filters.push_back(CreateFileFilter({kIntentActionView}, mime_types,
                                       file_extensions, handler.id,
                                       handler.include_directories));
    filters.back()->activity_label = extension->name();
  }

  return filters;
}

apps::IntentFilters CreateIntentFiltersForExtension(
    const extensions::Extension* extension) {
  return {};
}

apps::IntentFilterPtr CreateNoteTakingFilter() {
  auto intent_filter = std::make_unique<apps::IntentFilter>();
  intent_filter->AddSingleValueCondition(apps::ConditionType::kAction,
                                         kIntentActionCreateNote,
                                         apps::PatternMatchType::kLiteral);
  return intent_filter;
}

apps::IntentFilterPtr CreateLockScreenFilter() {
  auto intent_filter = std::make_unique<apps::IntentFilter>();
  intent_filter->AddSingleValueCondition(apps::ConditionType::kAction,
                                         kIntentActionStartOnLockScreen,
                                         apps::PatternMatchType::kLiteral);
  return intent_filter;
}


}  // namespace apps_util
