// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/enterprise/browser/reporting/report_util.h"

#include "base/files/file_path.h"
#include "base/json/json_writer.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "components/enterprise/connectors/core/reporting_constants.h"
#include "crypto/sha2.h"

namespace {

std::string SettingValueToString(
    enterprise_management::SettingValue setting_value) {
  switch (setting_value) {
    case enterprise_management::SettingValue::UNKNOWN:
      return "Unknown";
    case enterprise_management::SettingValue::DISABLED:
      return "Disabled";
    case enterprise_management::SettingValue::ENABLED:
      return "Enabled";
  }
}

base::ListValue RepeatedFieldptrToList(
    const google::protobuf::RepeatedPtrField<std::string>& field_values) {
  base::ListValue string_list;
  for (auto field_value : field_values) {
    string_list.Append(field_value);
  }

  return string_list;
}


}  // namespace

namespace enterprise_reporting {

namespace em = enterprise_management;

std::string ObfuscateFilePath(const std::string& file_path) {
  return crypto::SHA256HashString(file_path);
}

em::SettingValue TranslateSettingValue(
    device_signals::SettingValue setting_value) {
  switch (setting_value) {
    case device_signals::SettingValue::UNKNOWN:
      return em::SettingValue::UNKNOWN;
    case device_signals::SettingValue::DISABLED:
      return em::SettingValue::DISABLED;
    case device_signals::SettingValue::ENABLED:
      return em::SettingValue::ENABLED;
  }
}

em::ProfileSignalsReport::PasswordProtectionTrigger
TranslatePasswordProtectionTrigger(
    std::optional<safe_browsing::PasswordProtectionTrigger> trigger) {
  if (trigger == std::nullopt) {
    return em::ProfileSignalsReport::POLICY_UNSET;
  }
  switch (trigger.value()) {
    case safe_browsing::PasswordProtectionTrigger::PASSWORD_PROTECTION_OFF:
      return em::ProfileSignalsReport::PASSWORD_PROTECTION_OFF;
    case safe_browsing::PasswordProtectionTrigger::PASSWORD_REUSE:
      return em::ProfileSignalsReport::PASSWORD_REUSE;
    case safe_browsing::PasswordProtectionTrigger::PHISHING_REUSE:
      return em::ProfileSignalsReport::PHISHING_REUSE;
    case safe_browsing::PasswordProtectionTrigger::
        PASSWORD_PROTECTION_TRIGGER_MAX:
      NOTREACHED();
  }
}

em::ProfileSignalsReport::RealtimeUrlCheckMode TranslateRealtimeUrlCheckMode(
    enterprise_connectors::EnterpriseRealTimeUrlCheckMode mode) {
  switch (mode) {
    case enterprise_connectors::EnterpriseRealTimeUrlCheckMode::
        REAL_TIME_CHECK_DISABLED:
      return em::ProfileSignalsReport::DISABLED;
    case enterprise_connectors::EnterpriseRealTimeUrlCheckMode::
        REAL_TIME_CHECK_FOR_MAINFRAME_ENABLED:
      return em::ProfileSignalsReport::ENABLED_MAIN_FRAME;
  }
}

em::ProfileSignalsReport::SafeBrowsingLevel TranslateSafeBrowsingLevel(
    safe_browsing::SafeBrowsingState level) {
  switch (level) {
    case safe_browsing::SafeBrowsingState::NO_SAFE_BROWSING:
      return em::ProfileSignalsReport::NO_SAFE_BROWSING;
    case safe_browsing::SafeBrowsingState::STANDARD_PROTECTION:
      return em::ProfileSignalsReport::STANDARD_PROTECTION;
    case safe_browsing::SafeBrowsingState::ENHANCED_PROTECTION:
      return em::ProfileSignalsReport::ENHANCED_PROTECTION;
  }
}


std::string GetSecuritySignalsInReport(
    const em::ChromeProfileReportRequest& chrome_profile_report_request) {
  base::DictValue signals_dict;
  std::string signals_json;
  signals_dict.Set("Error", "No error found in report");

  if (chrome_profile_report_request.has_browser_device_identifier()) {
    auto browser_device_identifier =
        chrome_profile_report_request.browser_device_identifier();
    signals_dict.Set("display_name", browser_device_identifier.computer_name());
    signals_dict.Set("serial_number",
                     browser_device_identifier.serial_number());
    signals_dict.Set("host_name", browser_device_identifier.host_name());
  }

  if (chrome_profile_report_request.has_os_report()) {
    auto os_report = chrome_profile_report_request.os_report();
    signals_dict.Set("device_enrollment_domain",
                     os_report.device_enrollment_domain());
    signals_dict.Set("device_manufacturer", os_report.device_manufacturer());
    signals_dict.Set("device_model", os_report.device_model());
    signals_dict.Set("disk_encryption",
                     SettingValueToString(os_report.disk_encryption()));
    signals_dict.Set("mac_addresses",
                     RepeatedFieldptrToList(os_report.mac_addresses()));
    signals_dict.Set("operating_system", os_report.name());
    signals_dict.Set("os_firewall",
                     SettingValueToString(os_report.os_firewall()));
    signals_dict.Set("screen_lock_secured",
                     SettingValueToString(os_report.screen_lock_secured()));
    signals_dict.Set("system_dns_servers",
                     RepeatedFieldptrToList(os_report.system_dns_servers()));
    signals_dict.Set("os_version", os_report.version());

#if BUILDFLAG(IS_LINUX)
    if (os_report.has_distribution_version()) {
      signals_dict.Set("distribution_version",
                       os_report.distribution_version());
    }
#endif
  }

  if (!chrome_profile_report_request.has_browser_report()) {
    base::JSONWriter::WriteWithOptions(
        signals_dict, base::JSONWriter::OPTIONS_PRETTY_PRINT, &signals_json);
    return signals_json;
  }

  auto browser_report = chrome_profile_report_request.browser_report();
  signals_dict.Set("browser_version", browser_report.browser_version());

  if (browser_report.chrome_user_profile_infos_size() != 1) {
    base::JSONWriter::WriteWithOptions(
        signals_dict, base::JSONWriter::OPTIONS_PRETTY_PRINT, &signals_json);
    return signals_json;
  }

  auto chrome_user_profile_info = browser_report.chrome_user_profile_infos(0);
  signals_dict.Set("profile_id", chrome_user_profile_info.profile_id());

  if (chrome_user_profile_info.has_profile_signals_report()) {
    auto profile_signals_report =
        chrome_user_profile_info.profile_signals_report();
    signals_dict.Set("built_in_dns_client_enabled",
                     profile_signals_report.built_in_dns_client_enabled());
    signals_dict.Set(
        "chrome_remote_desktop_app_blocked",
        profile_signals_report.chrome_remote_desktop_app_blocked());
    signals_dict.Set(
        "password_protection_warning_trigger",
        static_cast<int>(
            profile_signals_report.password_protection_warning_trigger()));
    signals_dict.Set("profile_enrollment_domain",
                     profile_signals_report.profile_enrollment_domain());
    signals_dict.Set(
        "realtime_url_check_mode",
        static_cast<int>(profile_signals_report.realtime_url_check_mode()));
    signals_dict.Set(
        "safe_browsing_protection_level",
        static_cast<int>(
            profile_signals_report.safe_browsing_protection_level()));
    signals_dict.Set("site_isolation_enabled",
                     profile_signals_report.site_isolation_enabled());

    // Providers section
    signals_dict.Set("file_downloaded_providers",
                     RepeatedFieldptrToList(
                         profile_signals_report.file_downloaded_providers()));
    signals_dict.Set("file_attached_providers",
                     RepeatedFieldptrToList(
                         profile_signals_report.file_attached_providers()));
    signals_dict.Set("bulk_data_entry_providers",
                     RepeatedFieldptrToList(
                         profile_signals_report.bulk_data_entry_providers()));
    signals_dict.Set(
        "print_providers",
        RepeatedFieldptrToList(profile_signals_report.print_providers()));
    signals_dict.Set("security_event_providers",
                     RepeatedFieldptrToList(
                         profile_signals_report.security_event_providers()));

    if (chrome_profile_report_request.has_attestation_payload()) {
      auto attestation_payload =
          chrome_profile_report_request.attestation_payload();
      signals_dict.Set("attestation timestamp",
                       attestation_payload.timestamp());
      signals_dict.Set("attestation nonce", attestation_payload.nonce());
      // Do not print the attestation blob itself as it's too large to display
      // in logs.
      signals_dict.Set("attestation generation error",
                       attestation_payload.attestation_error());
    }
  }

  base::JSONWriter::WriteWithOptions(
      signals_dict, base::JSONWriter::OPTIONS_PRETTY_PRINT, &signals_json);

  return signals_json;
}

void RecordReportGenerationErrorMetric(ReportGenerationError error) {
  static constexpr char kReportGenerationErrorMetricsName[] =
      "Enterprise.CloudReportingReportGenerationError";
  base::UmaHistogramEnumeration(kReportGenerationErrorMetricsName, error);
}

}  // namespace enterprise_reporting
