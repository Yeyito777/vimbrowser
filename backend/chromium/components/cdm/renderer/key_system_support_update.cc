// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/cdm/renderer/key_system_support_update.h"

#include <stddef.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/containers/flat_set.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "build/build_config.h"
#include "components/cdm/common/buildflags.h"
#include "components/cdm/renderer/external_clear_key_key_system_info.h"
#include "content/public/renderer/key_system_support.h"
#include "content/public/renderer/render_frame.h"
#include "media/base/audio_codecs.h"
#include "media/base/cdm_capability.h"
#include "media/base/content_decryption_module.h"
#include "media/base/eme_constants.h"
#include "media/base/key_system_capability.h"
#include "media/base/key_system_info.h"
#include "media/base/media_switches.h"
#include "media/base/video_codecs.h"
#include "media/cdm/clear_key_cdm_common.h"
#include "media/media_buildflags.h"
#include "third_party/widevine/cdm/buildflags.h"

#if BUILDFLAG(ENABLE_WIDEVINE)
#include "components/cdm/renderer/widevine_key_system_info.h"
#include "third_party/widevine/cdm/widevine_cdm_common.h"  // nogncheck
#endif  // BUILDFLAG(ENABLE_WIDEVINE)


#if BUILDFLAG(ENABLE_PLAYREADY)
#include "components/cdm/common/playready_cdm_common.h"
#include "components/cdm/renderer/playready_key_system_info.h"
#include "media/base/supported_types.h"
#include "media/base/win/mf_feature_checks.h"
#endif  // BUILDFLAG(ENABLE_PLAYREADY)

using media::CdmSessionType;
using media::EmeFeatureSupport;
using media::KeySystemInfo;
using media::KeySystemInfos;
using media::SupportedCodecs;

namespace cdm {

namespace {

#if BUILDFLAG(ENABLE_WIDEVINE) || BUILDFLAG(IS_ANDROID) || \
    BUILDFLAG(ENABLE_PLAYREADY)
SupportedCodecs GetVP9Codecs(
    const base::flat_set<media::VideoCodecProfile>& profiles) {
  if (profiles.empty()) {
    // If no profiles are specified, then all are supported.
    return media::EME_CODEC_VP9_PROFILE0 | media::EME_CODEC_VP9_PROFILE2;
  }

  SupportedCodecs supported_vp9_codecs = media::EME_CODEC_NONE;
  for (const auto& profile : profiles) {
    switch (profile) {
      case media::VP9PROFILE_PROFILE0:
        supported_vp9_codecs |= media::EME_CODEC_VP9_PROFILE0;
        break;
      case media::VP9PROFILE_PROFILE2:
        supported_vp9_codecs |= media::EME_CODEC_VP9_PROFILE2;
        break;
      default:
        DVLOG(1) << "Unexpected " << GetCodecName(media::VideoCodec::kVP9)
                 << " profile: " << GetProfileName(profile);
        break;
    }
  }

  return supported_vp9_codecs;
}

#if BUILDFLAG(ENABLE_PLATFORM_HEVC)
SupportedCodecs GetHevcCodecs(
    const base::flat_set<media::VideoCodecProfile>& profiles) {

  // If no profiles are specified, then all are supported.
  if (profiles.empty()) {
    return media::EME_CODEC_HEVC_PROFILE_MAIN |
           media::EME_CODEC_HEVC_PROFILE_MAIN10;
  }

  SupportedCodecs supported_hevc_codecs = media::EME_CODEC_NONE;
  for (const auto& profile : profiles) {
    switch (profile) {
      case media::HEVCPROFILE_MAIN:
        supported_hevc_codecs |= media::EME_CODEC_HEVC_PROFILE_MAIN;
        break;
      case media::HEVCPROFILE_MAIN10:
        supported_hevc_codecs |= media::EME_CODEC_HEVC_PROFILE_MAIN10;
        break;
      default:
        DVLOG(1) << "Unexpected " << GetCodecName(media::VideoCodec::kHEVC)
                 << " profile: " << GetProfileName(profile);
        break;
    }
  }

  return supported_hevc_codecs;
}
#endif  // BUILDFLAG(ENABLE_PLATFORM_HEVC)

#if BUILDFLAG(ENABLE_PLATFORM_DOLBY_VISION)
// Dolby Vision HEVC-based profiles are only be supported when HEVC is enabled.
// However, this is enforced elsewhere, as DV profiles for both AVC and HEVC
// are returned here.
SupportedCodecs GetDolbyVisionCodecs(
    const base::flat_set<media::VideoCodecProfile>& profiles) {
  // If no profiles are specified, then all are supported.
  if (profiles.empty()) {
    return media::EME_CODEC_DOLBY_VISION_AVC |
           media::EME_CODEC_DOLBY_VISION_HEVC;
  }

  SupportedCodecs supported_dv_codecs = media::EME_CODEC_NONE;
  for (const auto& profile : profiles) {
    switch (profile) {
      case media::DOLBYVISION_PROFILE0:
        supported_dv_codecs |= media::EME_CODEC_DOLBY_VISION_PROFILE0;
        break;
      case media::DOLBYVISION_PROFILE5:
        supported_dv_codecs |= media::EME_CODEC_DOLBY_VISION_PROFILE5;
        break;
      case media::DOLBYVISION_PROFILE7:
        supported_dv_codecs |= media::EME_CODEC_DOLBY_VISION_PROFILE7;
        break;
      case media::DOLBYVISION_PROFILE8:
        supported_dv_codecs |= media::EME_CODEC_DOLBY_VISION_PROFILE8;
        break;
      case media::DOLBYVISION_PROFILE9:
        supported_dv_codecs |= media::EME_CODEC_DOLBY_VISION_PROFILE9;
        break;
      default:
        DVLOG(1) << "Unexpected "
                 << GetCodecName(media::VideoCodec::kDolbyVision)
                 << " profile: " << GetProfileName(profile);
        break;
    }
  }

  return supported_dv_codecs;
}
#endif  // BUILDFLAG(ENABLE_PLATFORM_DOLBY_VISION)

SupportedCodecs GetSupportedCodecs(const media::CdmCapability& capability,
                                   bool requires_clear_lead_support = true) {
  SupportedCodecs supported_codecs = media::EME_CODEC_NONE;

  for (const auto& codec : capability.audio_codecs) {
    switch (codec) {
      case media::AudioCodec::kOpus:
        supported_codecs |= media::EME_CODEC_OPUS;
        break;
      case media::AudioCodec::kVorbis:
        supported_codecs |= media::EME_CODEC_VORBIS;
        break;
      case media::AudioCodec::kFLAC:
        supported_codecs |= media::EME_CODEC_FLAC;
        break;
#if BUILDFLAG(USE_PROPRIETARY_CODECS)
      case media::AudioCodec::kAAC:
        supported_codecs |= media::EME_CODEC_AAC;
        break;
#if BUILDFLAG(ENABLE_PLATFORM_DTS_AUDIO)
      case media::AudioCodec::kDTS:
        supported_codecs |= media::EME_CODEC_DTS;
        supported_codecs |= media::EME_CODEC_DTSE;
        supported_codecs |= media::EME_CODEC_DTSXP2;
        break;
#endif  // BUILDFLAG(ENABLE_PLATFORM_DTS_AUDIO)
#if BUILDFLAG(ENABLE_PLATFORM_AC3_EAC3_AUDIO)
      case media::AudioCodec::kAC3:
        supported_codecs |= media::EME_CODEC_AC3;
        break;
      case media::AudioCodec::kEAC3:
        supported_codecs |= media::EME_CODEC_EAC3;
        break;
#endif  // BUILDFLAG(ENABLE_PLATFORM_AC3_EAC3_AUDIO)
#if BUILDFLAG(ENABLE_PLATFORM_AC4_AUDIO)
      case media::AudioCodec::kAC4:
        supported_codecs |= media::EME_CODEC_AC4;
        break;
#endif  // BUILDFLAG(ENABLE_PLATFORM_AC4_AUDIO)
#endif  // BUILDFLAG(USE_PROPRIETARY_CODECS)
      default:
        DVLOG(1) << "Unexpected supported codec: " << GetCodecName(codec);
        break;
    }
  }

  // For compatibility with older CDMs different profiles are only used
  // with some video codecs.
  for (const auto& [codec, video_codec_info] : capability.video_codecs) {
    if (requires_clear_lead_support && !video_codec_info.supports_clear_lead) {
      continue;
    }
    switch (codec) {
      case media::VideoCodec::kVP8:
        supported_codecs |= media::EME_CODEC_VP8;
        break;
      case media::VideoCodec::kVP9:
        supported_codecs |= GetVP9Codecs(video_codec_info.supported_profiles);
        break;
      case media::VideoCodec::kAV1:
        supported_codecs |= media::EME_CODEC_AV1;
        break;
#if BUILDFLAG(USE_PROPRIETARY_CODECS)
      case media::VideoCodec::kH264:
        supported_codecs |= media::EME_CODEC_AVC1;
        break;
#endif  // BUILDFLAG(USE_PROPRIETARY_CODECS)
#if BUILDFLAG(ENABLE_PLATFORM_HEVC)
      case media::VideoCodec::kHEVC:
        supported_codecs |= GetHevcCodecs(video_codec_info.supported_profiles);
        break;
#endif  // BUILDFLAG(ENABLE_PLATFORM_HEVC)
#if BUILDFLAG(ENABLE_PLATFORM_DOLBY_VISION)
      case media::VideoCodec::kDolbyVision:
        supported_codecs |=
            GetDolbyVisionCodecs(video_codec_info.supported_profiles);
        break;
#endif  // BUILDFLAG(ENABLE_PLATFORM_DOLBY_VISION)
      default:
        DVLOG(1) << "Unexpected supported codec: " << GetCodecName(codec);
        break;
    }
  }

  return supported_codecs;
}
#endif  // BUILDFLAG(ENABLE_WIDEVINE) || BUILDFLAG(IS_ANDROID) ||
        // BUILDFLAG(ENABLE_PLAYREADY)

#if BUILDFLAG(ENABLE_WIDEVINE)

// Returns whether persistent-license session can be supported.
bool CanSupportPersistentLicense() {
  if (!base::FeatureList::IsEnabled(media::kWidevinePersistentLicenseSupport)) {
    return false;
  }

#if BUILDFLAG(ENABLE_CDM_HOST_VERIFICATION) && \
    BUILDFLAG(ENABLE_CDM_STORAGE_ID)
  // On other platforms, persistent licenses are only supported if CDM host
  // verification and CDM storage ID are available.
  return true;

#else
  DVLOG_IF(2, !BUILDFLAG(ENABLE_CDM_HOST_VERIFICATION))
      << __func__ << ": Not supported without CDM host verification.";
  DVLOG_IF(2, !BUILDFLAG(ENABLE_CDM_STORAGE_ID))
      << __func__ << ": Not supported without CDM storage ID.";
  return false;

#endif  // BUILDFLAG(IS_CHROMEOS)
}

// Remove `kPersistentLicense` support if it's not supported by the platform.
base::flat_set<CdmSessionType> UpdatePersistentLicenseSupport(
    bool can_persist_data,
    base::flat_set<CdmSessionType> session_types) {
  // Do not support persistent-license if the process cannot persist data.
  if (!can_persist_data || !CanSupportPersistentLicense()) {
    session_types.erase(CdmSessionType::kPersistentLicense);
  }
  return session_types;
}

void AddWidevine(const media::KeySystemCapability& capability,
                 bool can_persist_data,
                 KeySystemInfos* key_systems) {

  // Codecs and encryption schemes.
  SupportedCodecs codecs = media::EME_CODEC_NONE;
  SupportedCodecs hw_secure_codecs = media::EME_CODEC_NONE;
  base::flat_set<::media::EncryptionScheme> encryption_schemes;
  base::flat_set<::media::EncryptionScheme> hw_secure_encryption_schemes;
  base::flat_set<CdmSessionType> session_types;
  base::flat_set<CdmSessionType> hw_secure_session_types;

  if (capability.sw_cdm_capability_or_status.has_value()) {
    const auto& sw_secure_capability =
        capability.sw_cdm_capability_or_status.value();
    codecs = GetSupportedCodecs(sw_secure_capability);
    encryption_schemes = sw_secure_capability.encryption_schemes;
    session_types = UpdatePersistentLicenseSupport(
        can_persist_data, sw_secure_capability.session_types);
    if (!session_types.contains(CdmSessionType::kTemporary)) {
      DVLOG(1) << "Temporary sessions must be supported.";
      return;
    }
    DVLOG(2) << "Software secure Widevine supported";
  } else {
    DVLOG(2) << "Software secure Widevine NOT supported";
  }

  if (capability.hw_cdm_capability_or_status.has_value()) {
    const auto& hw_secure_capability =
        capability.hw_cdm_capability_or_status.value();
    hw_secure_codecs = GetSupportedCodecs(hw_secure_capability);


    hw_secure_encryption_schemes = hw_secure_capability.encryption_schemes;
    hw_secure_session_types = UpdatePersistentLicenseSupport(
        can_persist_data, hw_secure_capability.session_types);
    if (!hw_secure_session_types.contains(CdmSessionType::kTemporary)) {
      DVLOG(1) << "Temporary sessions must be supported.";
      return;
    }
    DVLOG(2) << "Hardware secure Widevine supported";
  } else {
    DVLOG(2) << "Hardware secure Widevine NOT supported";
  }


  // Robustness.
  using Robustness = WidevineKeySystemInfo::Robustness;
  auto max_audio_robustness = Robustness::SW_SECURE_CRYPTO;
  auto max_video_robustness = Robustness::SW_SECURE_DECODE;


  // Others.
  auto persistent_state_support = EmeFeatureSupport::REQUESTABLE;
  auto distinctive_identifier_support = EmeFeatureSupport::NOT_SUPPORTED;
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_WIN)
  distinctive_identifier_support = EmeFeatureSupport::REQUESTABLE;
#endif

  key_systems->emplace_back(std::make_unique<WidevineKeySystemInfo>(
      codecs, encryption_schemes, session_types, hw_secure_codecs,
      hw_secure_encryption_schemes, hw_secure_session_types,
      max_audio_robustness, max_video_robustness, persistent_state_support,
      distinctive_identifier_support));

}
#endif  // BUILDFLAG(ENABLE_WIDEVINE)

void AddExternalClearKey(const media::KeySystemCapability& /*capability*/,
                         KeySystemInfos* key_systems) {
  DVLOG(1) << __func__;

  if (!base::FeatureList::IsEnabled(media::kExternalClearKeyForTesting)) {
    DLOG(ERROR) << "ExternalClearKey supported despite not enabled.";
    return;
  }

  // TODO(xhwang): Actually use `capability` to determine capabilities.
  key_systems->push_back(std::make_unique<ExternalClearKeyKeySystemInfo>());
}



void OnKeySystemSupportUpdated(
    bool can_persist_data,
    media::GetSupportedKeySystemsCB cb,
    content::KeySystemCapabilities key_system_capabilities) {
  KeySystemInfos key_systems;
  for (const auto& [key_system, capability] : key_system_capabilities) {
#if BUILDFLAG(ENABLE_WIDEVINE)
    if (key_system == kWidevineKeySystem) {
      AddWidevine(capability, can_persist_data, &key_systems);
      continue;
    }
#endif  // BUILDFLAG(ENABLE_WIDEVINE)

    if (key_system == media::kExternalClearKeyKeySystem) {
      AddExternalClearKey(capability, &key_systems);
      continue;
    }


    DLOG(ERROR) << "Unrecognized key system: " << key_system;
  }

  cb.Run(std::move(key_systems));
}

}  // namespace

std::unique_ptr<media::KeySystemSupportRegistration>
GetSupportedKeySystemsUpdates(content::RenderFrame* render_frame,
                              bool can_persist_data,
                              media::GetSupportedKeySystemsCB cb) {
  return content::ObserveKeySystemSupportUpdate(
      render_frame, base::BindRepeating(&OnKeySystemSupportUpdated,
                                        can_persist_data, std::move(cb)));
}

}  // namespace cdm
