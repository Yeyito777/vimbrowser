// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/cdm/renderer/widevine_key_system_info.h"

#include "base/command_line.h"
#include "base/feature_list.h"
#include "build/build_config.h"
#include "media/base/media_switches.h"
#include "media/media_buildflags.h"
#include "third_party/widevine/cdm/buildflags.h"
#include "third_party/widevine/cdm/widevine_cdm_common.h"

#if !BUILDFLAG(ENABLE_WIDEVINE)
#error This file should only be built when Widevine is enabled.
#endif

using media::CdmSessionType;
using media::EmeConfig;
using media::EmeConfigRuleState;
using media::EmeFeatureSupport;
using media::EmeInitDataType;
using media::EmeMediaType;
using media::EncryptionScheme;
using media::SupportedCodecs;
using Robustness = cdm::WidevineKeySystemInfo::Robustness;

namespace cdm {
namespace {

Robustness ConvertRobustness(const std::string& robustness) {
  if (robustness.empty())
    return Robustness::EMPTY;
  if (robustness == "SW_SECURE_CRYPTO")
    return Robustness::SW_SECURE_CRYPTO;
  if (robustness == "SW_SECURE_DECODE")
    return Robustness::SW_SECURE_DECODE;
  if (robustness == "HW_SECURE_CRYPTO")
    return Robustness::HW_SECURE_CRYPTO;
  if (robustness == "HW_SECURE_DECODE")
    return Robustness::HW_SECURE_DECODE;
  if (robustness == "HW_SECURE_ALL")
    return Robustness::HW_SECURE_ALL;
  return Robustness::INVALID;
}


}  // namespace

WidevineKeySystemInfo::WidevineKeySystemInfo(
    SupportedCodecs codecs,
    base::flat_set<EncryptionScheme> encryption_schemes,
    base::flat_set<CdmSessionType> session_types,
    SupportedCodecs hw_secure_codecs,
    base::flat_set<EncryptionScheme> hw_secure_encryption_schemes,
    base::flat_set<CdmSessionType> hw_secure_session_types,
    Robustness max_audio_robustness,
    Robustness max_video_robustness,
    EmeFeatureSupport persistent_state_support,
    EmeFeatureSupport distinctive_identifier_support)
    : codecs_(codecs),
      encryption_schemes_(std::move(encryption_schemes)),
      session_types_(std::move(session_types)),
      hw_secure_codecs_(hw_secure_codecs),
      hw_secure_encryption_schemes_(std::move(hw_secure_encryption_schemes)),
      hw_secure_session_types_(std::move(hw_secure_session_types)),
      max_audio_robustness_(max_audio_robustness),
      max_video_robustness_(max_video_robustness),
      persistent_state_support_(persistent_state_support),
      distinctive_identifier_support_(distinctive_identifier_support) {}

WidevineKeySystemInfo::~WidevineKeySystemInfo() = default;

std::string WidevineKeySystemInfo::GetBaseKeySystemName() const {
  return kWidevineKeySystem;
}

bool WidevineKeySystemInfo::IsSupportedKeySystem(
    const std::string& key_system) const {

  return key_system == kWidevineKeySystem;
}

bool WidevineKeySystemInfo::ShouldUseBaseKeySystemName() const {
  // Internally Widevine CDM only supports kWidevineKeySystem.
  return true;
}

bool WidevineKeySystemInfo::IsSupportedInitDataType(
    EmeInitDataType init_data_type) const {
  // Here we assume that support for a container implies support for the
  // associated initialization data type. KeySystems handles validating
  // |init_data_type| x |container| pairings.
  if (init_data_type == EmeInitDataType::WEBM)
    return (codecs_ & media::EME_CODEC_WEBM_ALL) != 0;
  if (init_data_type == EmeInitDataType::CENC)
    return (codecs_ & media::EME_CODEC_MP4_ALL) != 0;

  return false;
}

EmeConfig::Rule WidevineKeySystemInfo::GetEncryptionSchemeConfigRule(
    EncryptionScheme encryption_scheme) const {
  bool is_supported = encryption_schemes_.contains(encryption_scheme);
  bool is_hw_secure_supported =
      hw_secure_encryption_schemes_.contains(encryption_scheme);
  if (is_supported && is_hw_secure_supported) {
    return EmeConfig::SupportedRule();
  } else if (is_supported && !is_hw_secure_supported) {
    return EmeConfig{.hw_secure_codecs = EmeConfigRuleState::kNotAllowed};
  } else if (!is_supported && is_hw_secure_supported) {
    return EmeConfig{.hw_secure_codecs = EmeConfigRuleState::kRequired};
  } else {
    return media::EmeConfig::UnsupportedRule();
  }
}

SupportedCodecs WidevineKeySystemInfo::GetSupportedCodecs() const {
  return codecs_;
}

SupportedCodecs WidevineKeySystemInfo::GetSupportedHwSecureCodecs() const {
  return hw_secure_codecs_;
}

EmeConfig::Rule WidevineKeySystemInfo::GetRobustnessConfigRule(
    const std::string& key_system,
    EmeMediaType media_type,
    const std::string& requested_robustness,
    const bool* hw_secure_requirement) const {
  Robustness robustness = ConvertRobustness(requested_robustness);
  if (robustness == Robustness::INVALID) {
    return EmeConfig::UnsupportedRule();
  }

  Robustness max_robustness = Robustness::INVALID;
  switch (media_type) {
    case EmeMediaType::AUDIO:
      max_robustness = max_audio_robustness_;
      break;
    case EmeMediaType::VIDEO:
      max_robustness = max_video_robustness_;
      break;
  }

  // We can compare robustness levels whenever they are not HW_SECURE_CRYPTO
  // and SW_SECURE_DECODE in some order. If they are exactly those two then the
  // robustness requirement is not supported.
  if ((max_robustness == Robustness::HW_SECURE_CRYPTO &&
       robustness == Robustness::SW_SECURE_DECODE) ||
      (max_robustness == Robustness::SW_SECURE_DECODE &&
       robustness == Robustness::HW_SECURE_CRYPTO) ||
      robustness > max_robustness) {
    return media::EmeConfig::UnsupportedRule();
  }

  [[maybe_unused]] bool hw_secure_codecs_required =
      hw_secure_requirement && *hw_secure_requirement;

  // On other platforms, require hardware secure codecs for HW_SECURE_CRYPTO and
  // above.
  if (robustness >= Robustness::HW_SECURE_CRYPTO) {
    return EmeConfig{.hw_secure_codecs = EmeConfigRuleState::kRequired};
  }


  return media::EmeConfig::SupportedRule();
}

EmeConfig::Rule WidevineKeySystemInfo::GetPersistentLicenseSessionSupport()
    const {
  bool is_supported =
      session_types_.contains(CdmSessionType::kPersistentLicense);

  bool is_hw_secure_supported =
      hw_secure_session_types_.contains(CdmSessionType::kPersistentLicense);

  // Per GetPersistentLicenseSessionSupport() API, there's no need to specify
  // the PERSISTENCE requirement here, which is implicitly assumed and enforced
  // by `KeySystemConfigSelector`.
  if (is_supported && is_hw_secure_supported) {
    return EmeConfig::SupportedRule();
  } else if (is_supported && !is_hw_secure_supported) {
    return EmeConfig{.hw_secure_codecs = EmeConfigRuleState::kNotAllowed};
  } else if (!is_supported && is_hw_secure_supported) {
    return EmeConfig{.hw_secure_codecs = EmeConfigRuleState::kRequired};
  } else {
    return media::EmeConfig::UnsupportedRule();
  }
}

EmeFeatureSupport WidevineKeySystemInfo::GetPersistentStateSupport() const {
  return persistent_state_support_;
}

EmeFeatureSupport WidevineKeySystemInfo::GetDistinctiveIdentifierSupport()
    const {
  return distinctive_identifier_support_;
}

}  // namespace cdm
