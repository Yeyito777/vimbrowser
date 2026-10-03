// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crypto/process_bound_string.h"

#include "base/containers/span.h"
#include "build/build_config.h"

#include "third_party/boringssl/src/include/openssl/mem.h"

namespace crypto::internal {


size_t MaybeRoundUp(size_t size) {
  return size;
}

bool MaybeEncryptBuffer(base::span<uint8_t> buffer) {
  return false;
}

bool MaybeDecryptBuffer(base::span<uint8_t> buffer) {
  return false;
}

void SecureZeroBuffer(base::span<uint8_t> buffer) {
  OPENSSL_cleanse(buffer.data(), buffer.size());
}

}  // namespace crypto::internal
