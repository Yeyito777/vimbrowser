// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WEBAUTHN_UNEXPORTABLE_KEY_UTILS_H_
#define CHROME_BROWSER_WEBAUTHN_UNEXPORTABLE_KEY_UTILS_H_

#include <memory>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "build/build_config.h"
#include "crypto/user_verifying_key.h"



namespace crypto {
class UnexportableKeyProvider;
}  // namespace crypto

std::unique_ptr<crypto::UnexportableKeyProvider>
GetWebAuthnUnexportableKeyProvider();

std::unique_ptr<crypto::UserVerifyingKeyProvider>
GetWebAuthnUserVerifyingKeyProvider(
    crypto::UserVerifyingKeyProvider::Config config);


void SetWebAuthnUnexportableKeyProviderForTesting(
    std::unique_ptr<crypto::UnexportableKeyProvider> (*func)());

#endif  // CHROME_BROWSER_WEBAUTHN_UNEXPORTABLE_KEY_UTILS_H_
