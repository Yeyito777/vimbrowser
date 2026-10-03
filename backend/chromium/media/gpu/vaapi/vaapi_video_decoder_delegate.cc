// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/vaapi/vaapi_video_decoder_delegate.h"

#include "base/compiler_specific.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/default_tick_clock.h"
#include "build/build_config.h"
#include "media/base/cdm_context.h"
#include "media/gpu/vaapi/vaapi_decode_surface_handler.h"
#include "media/gpu/vaapi/vaapi_wrapper.h"


namespace media {

VaapiVideoDecoderDelegate::VaapiVideoDecoderDelegate(
    VaapiDecodeSurfaceHandler* const vaapi_dec,
    scoped_refptr<VaapiWrapper> vaapi_wrapper,
    ProtectedSessionUpdateCB on_protected_session_update_cb,
    CdmContext* cdm_context,
    EncryptionScheme encryption_scheme)
    : vaapi_dec_(vaapi_dec),
      vaapi_wrapper_(std::move(vaapi_wrapper)),
      on_protected_session_update_cb_(
          std::move(on_protected_session_update_cb)),
      encryption_scheme_(encryption_scheme),
      protected_session_state_(ProtectedSessionState::kNotCreated),
      performing_recovery_(false) {
  DCHECK(vaapi_wrapper_);
  DCHECK(vaapi_dec_);
  DETACH_FROM_SEQUENCE(sequence_checker_);
  transcryption_ = cdm_context && VaapiWrapper::GetImplementationType() ==
                                      VAImplementation::kMesaGallium;
}

VaapiVideoDecoderDelegate::~VaapiVideoDecoderDelegate() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Also destroy the protected session on destruction of the accelerator
  // delegate. That way if a new delegate is created, when it tries to create a
  // new protected session it won't overwrite the existing one.
  vaapi_wrapper_->DestroyProtectedSession();
}

void VaapiVideoDecoderDelegate::set_vaapi_wrapper(
    scoped_refptr<VaapiWrapper> vaapi_wrapper) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
  vaapi_wrapper_ = std::move(vaapi_wrapper);
  protected_session_state_ = ProtectedSessionState::kNotCreated;
  hw_identifier_.clear();
  hw_key_data_map_.clear();
}

void VaapiVideoDecoderDelegate::OnVAContextDestructionSoon() {}

bool VaapiVideoDecoderDelegate::HasInitiatedProtectedRecovery() {
  if (protected_session_state_ != ProtectedSessionState::kNeedsRecovery)
    return false;

  performing_recovery_ = true;
  protected_session_state_ = ProtectedSessionState::kNotCreated;
  return true;
}

bool VaapiVideoDecoderDelegate::SetDecryptConfig(
    std::unique_ptr<DecryptConfig> decrypt_config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // It is possible to switch between clear and encrypted (and vice versa).
  if (!decrypt_config)
    return true;
  decrypt_config_ = std::move(decrypt_config);
  encryption_scheme_ = decrypt_config_->encryption_scheme();
  return true;
}


bool VaapiVideoDecoderDelegate::NeedsProtectedSessionRecovery() {
  if (!IsEncryptedSession() || !vaapi_wrapper_->IsProtectedSessionDead() ||
      performing_recovery_) {
    return false;
  }

  RecoverProtectedSession();
  return true;
}

void VaapiVideoDecoderDelegate::ProtectedDecodedSucceeded() {
  performing_recovery_ = false;
}

std::string VaapiVideoDecoderDelegate::GetDecryptKeyId() const {
  DCHECK(decrypt_config_);
  return decrypt_config_->key_id();
}

void VaapiVideoDecoderDelegate::OnGetHwConfigData(
    bool success,
    const std::vector<uint8_t>& config_data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!success) {
    protected_session_state_ = ProtectedSessionState::kFailed;
    on_protected_session_update_cb_.Run(false);
    return;
  }

  hw_identifier_.clear();
  if (!vaapi_wrapper_->CreateProtectedSession(encryption_scheme_, config_data,
                                              &hw_identifier_)) {
    LOG(ERROR) << "Failed to setup protected session";
    protected_session_state_ = ProtectedSessionState::kFailed;
    on_protected_session_update_cb_.Run(false);
    return;
  }

  protected_session_state_ = ProtectedSessionState::kCreated;
  on_protected_session_update_cb_.Run(true);
}

void VaapiVideoDecoderDelegate::OnGetHwKeyData(
    const std::string& key_id,
    Decryptor::Status status,
    const std::vector<uint8_t>& key_data) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // There's a special case here where we are updating usage times/checking on
  // key validity, and in that case the key is already in the map.
  if (hw_key_data_map_.contains(key_id)) {
    if (status == Decryptor::Status::kSuccess)
      return;
    // This key is no longer valid, decryption will fail, so stop playback
    // now. This key should have been renewed by the CDM instead.
    LOG(ERROR) << "CDM has lost key information, stopping playback";
    protected_session_state_ = ProtectedSessionState::kFailed;
    on_protected_session_update_cb_.Run(false);
    return;
  }
  if (status != Decryptor::Status::kSuccess) {
    // If it's a failure, then indicate so, otherwise if it's waiting for a key,
    // then we don't do anything since we will get called again when there's a
    // message about key availability changing.
    if (status == Decryptor::Status::kNoKey) {
      DVLOG(1) << "HW did not have key information, keep waiting for it";
      return;
    }
    LOG(ERROR) << "Failure getting the key data, fail overall";
    protected_session_state_ = ProtectedSessionState::kFailed;
    on_protected_session_update_cb_.Run(false);
    return;
  }
  if (key_data.size() != DecryptConfig::kDecryptionKeySize) {
    LOG(ERROR) << "Invalid key size returned of: " << key_data.size();
    protected_session_state_ = ProtectedSessionState::kFailed;
    on_protected_session_update_cb_.Run(false);
    return;
  }
  hw_key_data_map_[key_id] = key_data;
  on_protected_session_update_cb_.Run(true);
}

void VaapiVideoDecoderDelegate::RecoverProtectedSession() {
  LOG(WARNING) << "Protected session loss detected, initiating recovery";
  protected_session_state_ = ProtectedSessionState::kNeedsRecovery;
  hw_key_data_map_.clear();
  hw_identifier_.clear();
}

}  // namespace media
