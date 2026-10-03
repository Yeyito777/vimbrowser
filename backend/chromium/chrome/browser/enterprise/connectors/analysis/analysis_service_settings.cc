// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/connectors/analysis/analysis_service_settings.h"

#include "build/build_config.h"
#include "components/enterprise/buildflags/buildflags.h"
#include "components/enterprise/connectors/core/common.h"
#include "components/enterprise/connectors/core/service_provider_config.h"


namespace enterprise_connectors {

AnalysisServiceSettings::AnalysisServiceSettings(
    const base::Value& settings_value,
    const ServiceProviderConfig& service_provider_config)
    : AnalysisServiceSettingsBase(settings_value, service_provider_config) {
  if (!analysis_config_) {
    // Parsing in the base class failed
    return;
  }


#if BUILDFLAG(ENTERPRISE_LOCAL_CONTENT_ANALYSIS)
  ParseVerificationSignatures(settings_value.GetDict());
#endif
}

#if BUILDFLAG(ENTERPRISE_LOCAL_CONTENT_ANALYSIS)
void AnalysisServiceSettings::ParseVerificationSignatures(
    const base::DictValue& settings_dict) {
#if BUILDFLAG(IS_MAC)
  const char* verification_key = kKeyMacVerification;
#elif BUILDFLAG(IS_LINUX)
  const char* verification_key = kKeyLinuxVerification;
#endif

  const base::ListValue* signatures =
      settings_dict.FindListByDottedPath(verification_key);
  if (!signatures) {
    return;
  }

  for (auto& v : *signatures) {
    if (v.is_string()) {
      verification_signatures_.push_back(v.GetString());
    }
  }
}
#endif

std::optional<AnalysisSettings> AnalysisServiceSettings::GetAnalysisSettings(
    const GURL& url,
    DataRegion data_region) const {
  auto settings =
      AnalysisServiceSettingsBase::GetAnalysisSettings(url, data_region);
  // If this is a cloud analysis (in which case the base class already
  // initialized the cloud-specific settings), return the settings as is.
  if (!settings.has_value() || is_cloud_analysis()) {
    return settings;
  }

  settings->cloud_or_local_settings =
      CloudOrLocalAnalysisSettings(GetLocalAnalysisSettings());

  return settings;
}

LocalAnalysisSettings AnalysisServiceSettings::GetLocalAnalysisSettings()
    const {
  CHECK(is_local_analysis());

  LocalAnalysisSettings local_settings;
  local_settings.local_path = analysis_config_->local_path;
  local_settings.user_specific = analysis_config_->user_specific;
  local_settings.subject_names = analysis_config_->subject_names;
  // We assume all support_tags structs have the same max file size.
  local_settings.max_file_size =
      analysis_config_->supported_tags[0].max_file_size;
  local_settings.verification_signatures = verification_signatures_;

  return local_settings;
}


AnalysisServiceSettings::AnalysisServiceSettings(AnalysisServiceSettings&&) =
    default;
AnalysisServiceSettings& AnalysisServiceSettings::operator=(
    AnalysisServiceSettings&&) = default;
AnalysisServiceSettings::~AnalysisServiceSettings() = default;

}  // namespace enterprise_connectors
