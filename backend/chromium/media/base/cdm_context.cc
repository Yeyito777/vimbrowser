// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/cdm_context.h"

#include "build/build_config.h"
#include "media/base/callback_registry.h"

namespace media {

CdmContext::CdmContext() = default;

CdmContext::~CdmContext() = default;

std::unique_ptr<CallbackRegistration> CdmContext::RegisterEventCB(
    EventCB /* event_cb */) {
  return nullptr;
}

Decryptor* CdmContext::GetDecryptor() {
  return nullptr;
}

std::optional<base::UnguessableToken> CdmContext::GetCdmId() const {
  return std::nullopt;
}

std::string CdmContext::CdmIdToString(const base::UnguessableToken* cdm_id) {
  return cdm_id ? cdm_id->ToString() : "null";
}





}  // namespace media
