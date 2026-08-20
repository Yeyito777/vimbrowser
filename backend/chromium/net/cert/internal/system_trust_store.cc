// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/internal/system_trust_store.h"

#include <memory>
#include <optional>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "crypto/crypto_buildflags.h"
#include "net/cert/x509_certificate.h"
#include "net/cert/x509_util.h"
#include "third_party/boringssl/src/pki/cert_errors.h"
#include "third_party/boringssl/src/pki/parsed_certificate.h"
#include "third_party/boringssl/src/pki/trust_store_collection.h"
#include "third_party/boringssl/src/pki/trust_store_in_memory.h"

#if BUILDFLAG(USE_NSS_CERTS)
#include "net/cert/internal/trust_store_nss.h"
#elif BUILDFLAG(IS_MAC)
#include <Security/Security.h>

#include "net/base/features.h"
#include "net/cert/internal/trust_store_mac.h"
#include "net/cert/x509_util_apple.h"
#endif

#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)
#include "net/cert/internal/trust_store_chrome.h"
#endif  // CHROME_ROOT_STORE_SUPPORTED


namespace net {


#if BUILDFLAG(CHROME_ROOT_STORE_SUPPORTED)
class SystemTrustStoreChromeWithUnOwnedSystemStore : public SystemTrustStore {
 public:
  // Creates a SystemTrustStore that gets publicly trusted roots from
  // |trust_store_chrome| and local trust settings from |trust_store_system|,
  // if non-null. Does not take ownership of |trust_store_system|, which must
  // outlive this object.
  explicit SystemTrustStoreChromeWithUnOwnedSystemStore(
      std::unique_ptr<TrustStoreChrome> trust_store_chrome,
      net::PlatformTrustStore* trust_store_system)
      : trust_store_chrome_(std::move(trust_store_chrome)),
        platform_trust_store_(trust_store_system) {
    if (trust_store_system) {
      trust_store_collection_.AddTrustStore(trust_store_system);
      non_crs_trust_store_collection_.AddTrustStore(trust_store_system);
    }

    trust_store_collection_.AddTrustStore(trust_store_chrome_.get());
  }

  bssl::TrustStore* GetTrustStore() override {
    return &trust_store_collection_;
  }

  // IsKnownRoot returns true if the given trust anchor is a standard one (as
  // opposed to a user-installed root)
  bool IsKnownRoot(const bssl::ParsedCertificate* trust_anchor) const override {
    return trust_store_chrome_->Contains(trust_anchor);
  }

  bool IsKnownMtcAnchor(const bssl::MTCAnchor* anchor) const override {
    return trust_store_chrome_->ContainsMTCAnchor(anchor);
  }

  bool IsLocallyTrustedRoot(
      const bssl::ParsedCertificate* trust_anchor) override {
    return non_crs_trust_store_collection_.GetTrust(trust_anchor)
        .IsTrustAnchor();
  }

  int64_t chrome_root_store_version() const override {
    return trust_store_chrome_->version();
  }

  std::optional<base::Time> mtc_metadata_update_time() const override {
    return trust_store_chrome_->mtc_metadata_update_time();
  }

  base::span<const ChromeRootCertConstraints> GetChromeRootConstraints(
      const bssl::CertPathBuilderResultPath* path) const override {
    return trust_store_chrome_->GetConstraintsForCert(path);
  }

  const TrustStoreChrome::MtcAnchorExtraData* GetMTCAnchorData(
      base::span<const uint8_t> log_id) const override {
    return trust_store_chrome_->GetMTCAnchorData(log_id);
  }

  bssl::TrustStore* eutl_trust_store() override {
    return trust_store_chrome_->eutl_trust_store();
  }

  net::PlatformTrustStore* GetPlatformTrustStore() override {
    return platform_trust_store_;
  }

 private:
  std::unique_ptr<TrustStoreChrome> trust_store_chrome_;
  bssl::TrustStoreCollection trust_store_collection_;
  bssl::TrustStoreCollection non_crs_trust_store_collection_;
  net::PlatformTrustStore* platform_trust_store_;
};

std::unique_ptr<SystemTrustStore> CreateChromeOnlySystemTrustStore(
    std::unique_ptr<TrustStoreChrome> chrome_root) {
  return std::make_unique<SystemTrustStoreChromeWithUnOwnedSystemStore>(
      std::move(chrome_root), /*trust_store_system=*/nullptr);
}

class SystemTrustStoreChrome
    : public SystemTrustStoreChromeWithUnOwnedSystemStore {
 public:
  // Creates a SystemTrustStore that gets publicly trusted roots from
  // |trust_store_chrome| and local trust settings from |trust_store_system|.
  explicit SystemTrustStoreChrome(
      std::unique_ptr<TrustStoreChrome> trust_store_chrome,
      std::unique_ptr<net::PlatformTrustStore> trust_store_system)
      : SystemTrustStoreChromeWithUnOwnedSystemStore(
            std::move(trust_store_chrome),
            trust_store_system.get()),
        trust_store_system_(std::move(trust_store_system)) {}

 private:
  std::unique_ptr<net::PlatformTrustStore> trust_store_system_;
};

std::unique_ptr<SystemTrustStore> CreateSystemTrustStoreChromeForTesting(
    std::unique_ptr<TrustStoreChrome> trust_store_chrome,
    std::unique_ptr<net::PlatformTrustStore> trust_store_system) {
  return std::make_unique<SystemTrustStoreChrome>(
      std::move(trust_store_chrome), std::move(trust_store_system));
}
#endif  // CHROME_ROOT_STORE_SUPPORTED

#if BUILDFLAG(USE_NSS_CERTS)

std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStoreChromeRoot(
    std::unique_ptr<TrustStoreChrome> chrome_root) {
  return std::make_unique<SystemTrustStoreChrome>(
      std::move(chrome_root), std::make_unique<TrustStoreNSS>(
                                  TrustStoreNSS::UseTrustFromAllUserSlots()));
}

#elif BUILDFLAG(IS_MAC)

namespace {

TrustStoreMac* GetGlobalTrustStoreMacForCRS() {
  constexpr TrustStoreMac::TrustImplType kDefaultMacTrustImplForCRS =
      TrustStoreMac::TrustImplType::kDomainCacheFullCerts;
  static base::NoDestructor<TrustStoreMac> static_trust_store_mac(
      kSecPolicyAppleSSL, kDefaultMacTrustImplForCRS);
  return static_trust_store_mac.get();
}

void InitializeTrustCacheForCRSOnWorkerThread() {
  GetGlobalTrustStoreMacForCRS()->InitializeTrustCache();
}

}  // namespace

std::unique_ptr<SystemTrustStore> CreateSslSystemTrustStoreChromeRoot(
    std::unique_ptr<TrustStoreChrome> chrome_root) {
  return std::make_unique<SystemTrustStoreChromeWithUnOwnedSystemStore>(
      std::move(chrome_root), GetGlobalTrustStoreMacForCRS());
}

void InitializeTrustStoreMacCache() {
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&InitializeTrustCacheForCRSOnWorkerThread));
}

#endif

}  // namespace net
