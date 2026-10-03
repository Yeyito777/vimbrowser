// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/password_store/login_database.h"

#include "base/metrics/histogram_macros.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/password_manager/core/browser/password_store/login_database.h"

namespace password_manager {

namespace {

enum class PasswordDecryptionResult {
  kFailed = 0,
  kSucceeded = 1,
  kSucceededBySkipping = 2,
  kSucceededByIgnoringFailure = 3,
  kMaxValue = kSucceededByIgnoringFailure
};

void RecordPasswordDecryptionResult(PasswordDecryptionResult result) {
  UMA_HISTOGRAM_ENUMERATION("PasswordManager.StoreDecryptionResult", result);
}

}  // namespace

EncryptionResult LoginDatabase::EncryptedString(
    const std::u16string& plain_text,
    std::string* cipher_text) const {
  bool result =
      encryptor_ && encryptor_->EncryptString16(plain_text, cipher_text);
  return result ? EncryptionResult::kSuccess
                : EncryptionResult::kServiceFailure;
}

EncryptionResult LoginDatabase::DecryptedString(
    const std::string& cipher_text,
    std::u16string* plain_text) const {
  bool use_encryption = true;

  if (!use_encryption) {
    *plain_text = base::UTF8ToUTF16(cipher_text);
    RecordPasswordDecryptionResult(
        PasswordDecryptionResult::kSucceededBySkipping);
    return EncryptionResult::kSuccess;
  }

  bool decryption_success =
      encryptor_ && encryptor_->DecryptString16(cipher_text, plain_text);
  RecordPasswordDecryptionResult(decryption_success
                                     ? PasswordDecryptionResult::kSucceeded
                                     : PasswordDecryptionResult::kFailed);
  return decryption_success ? EncryptionResult::kSuccess
                            : EncryptionResult::kServiceFailure;
}

}  // namespace password_manager
