// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/download/download_dir_util.h"

#include "base/files/file_path.h"
#include "build/build_config.h"
#include "chrome/browser/policy/policy_path_parser.h"
#include "chrome/common/chrome_features.h"
#include "components/policy/core/browser/configuration_policy_handler_parameters.h"


namespace download_dir_util {


base::FilePath::StringType ExpandDownloadDirectoryPath(
    const base::FilePath::StringType& string_value,
    const policy::PolicyHandlerParameters& parameters) {
  return policy::path_parser::ExpandPathVariables(string_value);
}

}  // namespace download_dir_util
