// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/certificate_manager/client_cert_sources.h"

#include <map>
#include <optional>
#include <string>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_view_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/resources/certificate_manager/certificate_manager.mojom.h"
#include "chrome/browser/ui/chrome_select_file_policy.h"
#include "chrome/browser/ui/webui/certificate_manager/certificate_manager_utils.h"
#include "chrome/common/net/x509_certificate_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/browser_thread.h"
#include "crypto/crypto_buildflags.h"
#include "crypto/sha2.h"
#include "net/base/hash_value.h"
#include "net/base/net_errors.h"
#include "net/cert/x509_certificate.h"
#include "net/ssl/client_cert_identity.h"
#include "net/ssl/client_cert_store.h"
#include "net/ssl/client_cert_store_empty.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"

#if BUILDFLAG(USE_NSS_CERTS)
#include "chrome/browser/net/nss_service.h"
#include "chrome/browser/net/nss_service_factory.h"
#include "chrome/browser/ui/crypto_module_delegate_nss.h"
#include "net/cert/nss_cert_database.h"
#include "net/cert/x509_util_nss.h"
#include "net/ssl/client_cert_store_nss.h"
#endif  // BUILDFLAG(USE_NSS_CERTS)


#if BUILDFLAG(IS_MAC)
#include "net/ssl/client_cert_store_mac.h"
#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
#include "chrome/browser/browser_process.h"
#include "chrome/browser/enterprise/client_certificates/certificate_provisioning_service_factory.h"
#include "chrome/browser/policy/chrome_browser_policy_connector.h"
#include "components/enterprise/browser/controller/chrome_browser_cloud_management_controller.h"
#include "components/enterprise/client_certificates/core/certificate_provisioning_service.h"
#include "components/enterprise/client_certificates/core/client_certificates_service.h"
#include "components/enterprise/client_certificates/core/features.h"
#endif


namespace {

class ClientCertStoreFactory {
 public:
  virtual ~ClientCertStoreFactory() = default;
  virtual std::unique_ptr<net::ClientCertStore> CreateClientCertStore() = 0;
};

// A certificate loader that wraps a ClientCertStoreFactory. Read-only.
class ClientCertStoreLoader {
 public:
  explicit ClientCertStoreLoader(
      std::unique_ptr<ClientCertStoreFactory> factory)
      : factory_(std::move(factory)) {}

  // Lifetimes note: The callback will not be called if the
  // ClientCertStoreLoader (and thus, the ClientCertStore handle held by
  // `active_requests_`) is destroyed first.
  void GetCerts(base::OnceCallback<void(net::CertificateList)> callback) {
    std::unique_ptr<net::ClientCertStore> store =
        factory_->CreateClientCertStore();
    net::ClientCertStore* store_ptr = store.get();
    active_requests_[store_ptr] = std::move(store);
    // Unretained is safe as the callback is not run if `active_requests_` is
    // destroyed.
    store_ptr->GetClientCerts(
        base::MakeRefCounted<net::SSLCertRequestInfo>(),
        base::BindOnce(&ClientCertStoreLoader::HandleClientCertsResult,
                       base::Unretained(this), store_ptr, std::move(callback)));
  }

 private:
  void HandleClientCertsResult(
      net::ClientCertStore* store,
      base::OnceCallback<void(net::CertificateList)> callback,
      net::ClientCertIdentityList identities) {
    net::CertificateList certs;
    certs.reserve(identities.size());
    for (const auto& identity : identities) {
      certs.push_back(identity->certificate());
    }
    active_requests_.erase(store);
    std::move(callback).Run(std::move(certs));
  }

  std::unique_ptr<ClientCertStoreFactory> factory_;
  std::map<net::ClientCertStore*, std::unique_ptr<net::ClientCertStore>>
      active_requests_;
};

#if BUILDFLAG(IS_LINUX)
class ClientCertStoreFactoryNSS : public ClientCertStoreFactory {
 public:
  std::unique_ptr<net::ClientCertStore> CreateClientCertStore() override {
    return std::make_unique<net::ClientCertStoreNSS>(
        base::BindRepeating(&CreateCryptoModuleBlockingPasswordDelegate,
                            kCryptoModulePasswordClientAuth));
  }
};
#elif BUILDFLAG(IS_MAC)
class ClientCertStoreFactoryMac : public ClientCertStoreFactory {
 public:
  std::unique_ptr<net::ClientCertStore> CreateClientCertStore() override {
    return std::make_unique<net::ClientCertStoreMac>();
  }
};
#endif

#if !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_LINUX)
std::unique_ptr<ClientCertStoreLoader> CreatePlatformClientCertLoader(
    Profile* profile) {
#if BUILDFLAG(IS_MAC)
  return std::make_unique<ClientCertStoreLoader>(
      std::make_unique<ClientCertStoreFactoryMac>());
#else
  return nullptr;
#endif
}
#endif

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
class ClientCertStoreFactoryProvisioned : public ClientCertStoreFactory {
 public:
  explicit ClientCertStoreFactoryProvisioned(
      client_certificates::CertificateProvisioningService*
          profile_provisioning_service,
      client_certificates::CertificateProvisioningService*
          browser_provisioning_service)
      : profile_provisioning_service_(profile_provisioning_service),
        browser_provisioning_service_(browser_provisioning_service) {}

  std::unique_ptr<net::ClientCertStore> CreateClientCertStore() override {
    return client_certificates::ClientCertificatesService::Create(
        profile_provisioning_service_, browser_provisioning_service_,
        std::make_unique<net::ClientCertStoreEmpty>());
  }

 private:
  raw_ptr<client_certificates::CertificateProvisioningService>
      profile_provisioning_service_;
  raw_ptr<client_certificates::CertificateProvisioningService>
      browser_provisioning_service_;
};

std::unique_ptr<ClientCertStoreLoader> CreateProvisionedClientCertLoader(
    Profile* profile) {
  client_certificates::CertificateProvisioningService*
      profile_provisioning_service = nullptr;
  if (profile) {
    profile_provisioning_service = client_certificates::
        CertificateProvisioningServiceFactory::GetForProfile(profile);
  }

  client_certificates::CertificateProvisioningService*
      browser_provisioning_service =
          g_browser_process->browser_policy_connector()
              ->chrome_browser_cloud_management_controller()
              ->GetCertificateProvisioningService();

  if (!profile_provisioning_service && !browser_provisioning_service) {
    return nullptr;
  }

  return std::make_unique<ClientCertStoreLoader>(
      std::make_unique<ClientCertStoreFactoryProvisioned>(
          profile_provisioning_service, browser_provisioning_service));
}
#endif

void PopulateCertInfosFromCertificateList(
    CertificateManagerPageHandler::GetCertificatesCallback callback,
    const net::CertificateList& certs,
    bool is_deletable) {
  std::vector<certificate_manager::mojom::SummaryCertInfoPtr> out_infos;
  for (const auto& cert : certs) {
    x509_certificate_model::X509CertificateModel model(
        bssl::UpRef(cert->cert_buffer()));
    out_infos.push_back(certificate_manager::mojom::SummaryCertInfo::New(
        model.HashCertSHA256(), model.GetTitle(), is_deletable));
  }
  std::move(callback).Run(std::move(out_infos));
}

net::X509Certificate* FindCertificateFromCertificateList(
    std::string_view sha256_hex_hash,
    const net::CertificateList& certs) {
  net::SHA256HashValue hash;
  if (!base::HexStringToSpan(sha256_hex_hash, hash)) {
    return nullptr;
  }

  for (const auto& cert : certs) {
    if (net::X509Certificate::CalculateFingerprint256(cert->cert_buffer()) ==
        hash) {
      return cert.get();
    }
  }

  return nullptr;
}

void ViewCertificateFromCertificateList(
    const std::string& sha256_hex_hash,
    const net::CertificateList& certs,
    base::WeakPtr<content::WebContents> web_contents) {
  if (!web_contents) {
    return;
  }

  net::X509Certificate* cert =
      FindCertificateFromCertificateList(sha256_hex_hash, certs);
  if (cert) {
    ShowCertificateDialog(std::move(web_contents),
                          bssl::UpRef(cert->cert_buffer()));
  }
}

class ClientCertSource : public CertificateManagerPageHandler::CertSource {
 public:
  explicit ClientCertSource(std::unique_ptr<ClientCertStoreLoader> loader)
      : loader_(std::move(loader)) {}
  ~ClientCertSource() override = default;

  void GetCertificateInfos(
      CertificateManagerPageHandler::GetCertificatesCallback callback)
      override {
    if (certs_) {
      ReplyToGetCertificatesCallback(std::move(callback));
      return;
    }
    RefreshCachedCertificateList(
        base::BindOnce(&ClientCertSource::ReplyToGetCertificatesCallback,
                       base::Unretained(this), std::move(callback)));
  }

  void ViewCertificate(
      const std::string& sha256_hex_hash,
      base::WeakPtr<content::WebContents> web_contents) override {
    if (!loader_ || !certs_) {
      return;
    }
    ViewCertificateFromCertificateList(sha256_hex_hash, *certs_,
                                       std::move(web_contents));
  }

 private:
  // Refresh list of cached certificates and run `callback` when done.
  void RefreshCachedCertificateList(base::OnceClosure callback) {
    if (!loader_) {
      std::move(callback).Run();
      return;
    }
    // Unretained is safe here as if `this` is destroyed, the ClientCertStore
    // will be destroyed, and the ClientCertStore contract is that the callback
    // will not be called after the ClientCertStore object is destroyed.
    loader_->GetCerts(base::BindOnce(&ClientCertSource::SaveCertsAndRespond,
                                     base::Unretained(this),
                                     std::move(callback)));
  }

  void ReplyToGetCertificatesCallback(
      CertificateManagerPageHandler::GetCertificatesCallback callback) const {
    PopulateCertInfosFromCertificateList(std::move(callback), *certs_,
                                         /*is_deletable=*/false);
  }

  void SaveCertsAndRespond(base::OnceClosure callback,
                           net::CertificateList certs) {
    certs_ = std::move(certs);
    std::move(callback).Run();
  }

  std::unique_ptr<ClientCertStoreLoader> loader_;
  std::optional<net::CertificateList> certs_;
};

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)
// ChromeOS currently can use either Kcer or NSS for listing client certs, and
// Linux uses NSS only. This interface provides an abstraction to hide that
// from WritableClientCertSource. Currently this class only handles reading
// from the database and writing is still handled directly inside
// WritableClientCertSource (even when Kcer is enabled, the ChromeOS code still
// writes to both NSS and Kcer). Once NSS client cert support is removed on
// ChromeOS, consider if things should be refactored as then NSS will only be
// used on Linux and Kcer will only be used on ChromeOS.
class WritableCertLoader : public CertificateManagerPageHandler::CertSource {
 public:
  virtual void RefreshCachedCertificateList(base::OnceClosure callback) = 0;

  void GetCertificateInfos(
      CertificateManagerPageHandler::GetCertificatesCallback callback)
      override {
    if (certs_) {
      ReplyToGetCertificatesCallback(std::move(callback));
      return;
    }
    RefreshCachedCertificateList(
        base::BindOnce(&WritableCertLoader::ReplyToGetCertificatesCallback,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
  }

  void ViewCertificate(
      const std::string& sha256_hex_hash,
      base::WeakPtr<content::WebContents> web_contents) override {
    if (!certs_) {
      return;
    }

    net::X509Certificate* cert = FindCertificate(sha256_hex_hash);
    if (cert) {
      ShowCertificateDialog(std::move(web_contents),
                            bssl::UpRef(cert->cert_buffer()));
    }
  }

  net::X509Certificate* FindCertificate(
      std::string_view sha256_hex_hash) const {
    if (!certs_) {
      return nullptr;
    }

    net::SHA256HashValue hash;
    if (!base::HexStringToSpan(sha256_hex_hash, hash)) {
      return nullptr;
    }

    for (const auto& info : *certs_) {
      if (net::X509Certificate::CalculateFingerprint256(
              info.cert->cert_buffer()) == hash) {
        return info.cert.get();
      }
    }

    return nullptr;
  }

 protected:
  struct CertInfo {
    scoped_refptr<net::X509Certificate> cert;
    bool is_deletable;
  };

  void ReplyToGetCertificatesCallback(
      CertificateManagerPageHandler::GetCertificatesCallback callback) const {
    std::vector<certificate_manager::mojom::SummaryCertInfoPtr> out_infos;
    for (const auto& info : *certs_) {
      x509_certificate_model::X509CertificateModel model(
          bssl::UpRef(info.cert->cert_buffer()));
      out_infos.push_back(certificate_manager::mojom::SummaryCertInfo::New(
          model.HashCertSHA256(), model.GetTitle(), info.is_deletable));
    }
    std::move(callback).Run(std::move(out_infos));
  }

  std::optional<std::vector<CertInfo>> certs_;

 private:
  base::WeakPtrFactory<WritableCertLoader> weak_ptr_factory_{this};
};


class NSSLoader : public WritableCertLoader {
 public:
  explicit NSSLoader(Profile* profile)
      : profile_(profile),
        loader_(std::make_unique<ClientCertStoreLoader>(
            std::make_unique<ClientCertStoreFactoryNSS>())) {}
  ~NSSLoader() override = default;

  void RefreshCachedCertificateList(base::OnceClosure callback) override {
    if (!loader_) {
      std::move(callback).Run();
      return;
    }

    loader_->GetCerts(base::BindOnce(&NSSLoader::SaveCertsAndRespond,
                                     weak_ptr_factory_.GetWeakPtr(),
                                     std::move(callback)));
  }

 private:
  void SaveCertsAndRespond(base::OnceClosure callback,
                           net::CertificateList certs) {
    ClientCertManagementAccessControls policy(profile_);
    // TODO(crbug.com/40928765): This should actually be set by checking
    // ClientCertManagementAccessControls.IsChangeAllowed on a per-cert basis.
    // However listing certs using kcer is already the default so it's
    // questionable whether spending the effort to implement it correctly for
    // the NSS implementation is worth doing. In any case the correct behavior
    // is still enforced if the user tried to delete a cert where it mattered.
    const bool is_deletable = policy.IsManagementAllowed(
        ClientCertManagementAccessControls::kSoftwareBacked);
    certs_ = std::vector<CertInfo>();
    certs_->reserve(certs.size());
    for (scoped_refptr<net::X509Certificate>& cert : certs) {
      certs_->emplace_back(cert, is_deletable);
    }

    std::move(callback).Run();
  }

 private:
  raw_ptr<Profile> profile_;
  std::unique_ptr<ClientCertStoreLoader> loader_;
  base::WeakPtrFactory<NSSLoader> weak_ptr_factory_{this};
};

// Subclass of ClientCertSource that also allows importing client certificates
// to the ChromeOS or Linux client cert store.
class WritableClientCertSource
    : public CertificateManagerPageHandler::CertSource,
      public ui::SelectFileDialog::Listener {
 public:
  explicit WritableClientCertSource(
      mojo::Remote<certificate_manager::mojom::CertificateManagerPage>*
          remote_client,
      Profile* profile)
      : remote_client_(remote_client), profile_(profile) {
    cert_loader_ = std::make_unique<NSSLoader>(profile);
  }

  ~WritableClientCertSource() override {
    if (select_file_dialog_) {
      select_file_dialog_->ListenerDestroyed();
    }
  }

  void GetCertificateInfos(
      CertificateManagerPageHandler::GetCertificatesCallback callback)
      override {
    cert_loader_->GetCertificateInfos(std::move(callback));
  }

  void ViewCertificate(
      const std::string& sha256_hex_hash,
      base::WeakPtr<content::WebContents> web_contents) override {
    cert_loader_->ViewCertificate(sha256_hex_hash, std::move(web_contents));
  }

  void ImportCertificate(
      base::WeakPtr<content::WebContents> web_contents,
      CertificateManagerPageHandler::ImportCertificateCallback callback)
      override {
    BeginImportCertificate(/*hardware_backed=*/false, std::move(web_contents),
                           std::move(callback));
  }

  void ImportAndBindCertificate(
      base::WeakPtr<content::WebContents> web_contents,
      CertificateManagerPageHandler::ImportCertificateCallback callback)
      override {
    BeginImportCertificate(/*hardware_backed=*/true, std::move(web_contents),
                           std::move(callback));
  }

  void DeleteCertificate(
      const std::string& display_name,
      const std::string& sha256hash_hex,
      CertificateManagerPageHandler::DeleteCertificateCallback callback)
      override {
    (*remote_client_)
        ->AskForConfirmation(
            l10n_util::GetStringFUTF8(
                IDS_SETTINGS_CERTIFICATE_MANAGER_V2_DELETE_CERT_TITLE,
                base::UTF8ToUTF16(display_name)),
            l10n_util::GetStringUTF8(
                IDS_SETTINGS_CERTIFICATE_MANAGER_V2_DELETE_CLIENT_CERT_DESCRIPTION),
            base::BindOnce(
                &WritableClientCertSource::GotDeleteCertificateConfirmation,
                weak_ptr_factory_.GetWeakPtr(), sha256hash_hex,
                std::move(callback)));
  }

  void BeginImportCertificate(
      bool hardware_backed,
      base::WeakPtr<content::WebContents> web_contents,
      CertificateManagerPageHandler::ImportCertificateCallback callback) {
    // Containing web contents went away (e.g. user navigated away) or dialog
    // is already open. Don't try to open the dialog.
    if (!web_contents || select_file_dialog_) {
      std::move(callback).Run(nullptr);
      return;
    }

    if (!ClientCertManagementAccessControls(profile_).IsManagementAllowed(
            hardware_backed
                ? ClientCertManagementAccessControls::kHardwareBacked
                : ClientCertManagementAccessControls::kSoftwareBacked)) {
      // This error is not expected to be displayed under normal circumstances,
      // so it's not localized.
      std::move(callback).Run(
          certificate_manager::mojom::ActionResult::NewError("not allowed"));
      return;
    }

    import_hardware_backed_ = hardware_backed;
    import_callback_ = std::move(callback);

    select_file_dialog_ = ui::SelectFileDialog::Create(
        this, std::make_unique<ChromeSelectFilePolicy>(web_contents.get()));

    ui::SelectFileDialog::FileTypeInfo file_type_info;
    file_type_info.extensions = {{FILE_PATH_LITERAL("p12"),
                                  FILE_PATH_LITERAL("pfx"),
                                  FILE_PATH_LITERAL("crt")}};
    file_type_info.include_all_files = true;
    select_file_dialog_->SelectFile(
        ui::SelectFileDialog::SELECT_OPEN_FILE, std::u16string(),
        base::FilePath(), &file_type_info,
        1,  // 1-based index for |file_type_info.extensions| to specify default.
        FILE_PATH_LITERAL("p12"), web_contents->GetTopLevelNativeWindow(),
        /*caller=*/nullptr);
  }

  // ui::SelectFileDialog::Listener
  void FileSelected(const ui::SelectedFileInfo& file, int index) override {
    select_file_dialog_ = nullptr;

    // Use CONTINUE_ON_SHUTDOWN since this is only for reading a file, if it
    // doesn't complete before shutdown the file still exists, and even if the
    // browser blocked on completing this task, the import isn't actually done
    // yet, so just blocking shutdown on the file read wouldn't accomplish
    // anything. CONTINUE_ON_SHUTDOWN should be safe as base::ReadFileToBytes
    // doesn't access any global state.
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE,
        {base::MayBlock(), base::TaskPriority::USER_BLOCKING,
         base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
        base::BindOnce(&base::ReadFileToBytes, file.path()),
        base::BindOnce(&WritableClientCertSource::FileRead,
                       weak_ptr_factory_.GetWeakPtr()));
  }

  void FileSelectionCanceled() override {
    select_file_dialog_ = nullptr;

    std::move(import_callback_).Run(nullptr);
  }

 private:
  void FileRead(std::optional<std::vector<uint8_t>> file_bytes) {
    if (!file_bytes) {
      std::move(import_callback_)
          .Run(certificate_manager::mojom::ActionResult::NewError(
              l10n_util::GetStringUTF8(
                  IDS_SETTINGS_CERTIFICATE_MANAGER_V2_READ_FILE_ERROR)));
      return;
    }

    (*remote_client_)
        ->AskForImportPassword(base::BindOnce(
            &WritableClientCertSource::GotImportPassword,
            weak_ptr_factory_.GetWeakPtr(), std::move(*file_bytes)));
  }

  void GotImportPassword(std::vector<uint8_t> file_bytes,
                         const std::optional<std::string>& password) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (!password) {
      std::move(import_callback_).Run(nullptr);
      return;
    }

    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            &WritableClientCertSource::GetCertDBOnIOThread,
            NssServiceFactory::GetForContext(profile_)
                ->CreateNSSCertDatabaseGetterForIOThread(),
            base::BindOnce(
                &WritableClientCertSource::
                    GotNSSCertDatabaseForImportOnIOThread,
                import_hardware_backed_, std::move(file_bytes), *password,
                base::BindOnce(&WritableClientCertSource::FinishedNSSImport,
                               weak_ptr_factory_.GetWeakPtr()))));
  }

  static void GetCertDBOnIOThread(
      NssCertDatabaseGetter database_getter,
      base::OnceCallback<void(net::NSSCertDatabase*)> callback) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

    auto split_callback = base::SplitOnceCallback(std::move(callback));

    net::NSSCertDatabase* cert_db =
        std::move(database_getter).Run(std::move(split_callback.first));
    // If the NSS database was already available, |cert_db| is non-null and
    // |did_get_cert_db_callback| has not been called. Call it explicitly.
    if (cert_db) {
      std::move(split_callback.second).Run(cert_db);
    }
  }

  static void GotNSSCertDatabaseForImportOnIOThread(
      bool use_hardware_backed,
      std::vector<uint8_t> file_bytes,
      std::string password,
      base::OnceCallback<void(std::vector<uint8_t> file_bytes,
                              std::string password,
                              int nss_import_result)> finished_import_callback,
      net::NSSCertDatabase* cert_db) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

    crypto::ScopedPK11Slot slot;
    if (use_hardware_backed) {
      slot = cert_db->GetPrivateSlot();
    } else {
      slot = cert_db->GetPublicSlot();
    }
    bool is_extractable = !use_hardware_backed;
    // TODO(crbug.com/40928765): Should do the NSS import on worker thread, not
    // IO thread. (Would need to add an ImportFromPKCS12Async method on
    // NSSCertDatabase.)
    int nss_import_result = cert_db->ImportFromPKCS12(
        slot.get(), std::string(base::as_string_view(file_bytes)),
        base::UTF8ToUTF16(password), is_extractable, nullptr);

    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(std::move(finished_import_callback),
                                  std::move(file_bytes), std::move(password),
                                  nss_import_result));
  }

  void FinishedNSSImport(std::vector<uint8_t> file_bytes,
                         std::string password,
                         int nss_import_result) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);


    ReplyToImportCallback(nss_import_result);
  }


  void ReplyToImportCallback(int nss_import_result) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

    if (nss_import_result == net::OK) {
      // Refresh the certificate list to include the newly imported cert, and
      // call the import complete callback once the list has been updated.
      cert_loader_->RefreshCachedCertificateList(base::BindOnce(
          std::move(import_callback_),
          certificate_manager::mojom::ActionResult::NewSuccess(
              certificate_manager::mojom::SuccessResult::kSuccess)));
    } else {
      // TODO(crbug.com/40928765): If the error was bad password, could prompt
      // the user to try again rather than just failing and requiring the user
      // to reselect the file to try again.
      int message_id;
      switch (nss_import_result) {
        case net::ERR_PKCS12_IMPORT_BAD_PASSWORD:
          message_id = IDS_SETTINGS_CERTIFICATE_MANAGER_V2_IMPORT_BAD_PASSWORD;
          break;
        case net::ERR_PKCS12_IMPORT_INVALID_MAC:
          message_id = IDS_SETTINGS_CERTIFICATE_MANAGER_V2_IMPORT_INVALID_MAC;
          break;
        case net::ERR_PKCS12_IMPORT_INVALID_FILE:
          message_id = IDS_SETTINGS_CERTIFICATE_MANAGER_V2_IMPORT_INVALID_FILE;
          break;
        case net::ERR_PKCS12_IMPORT_UNSUPPORTED:
          message_id = IDS_SETTINGS_CERTIFICATE_MANAGER_V2_IMPORT_UNSUPPORTED;
          break;
        default:
          message_id = IDS_SETTINGS_CERTIFICATE_MANAGER_V2_IMPORT_FAILED;
      }
      std::move(import_callback_)
          .Run(certificate_manager::mojom::ActionResult::NewError(
              l10n_util::GetStringUTF8(message_id)));
    }
  }

  void GotDeleteCertificateConfirmation(
      const std::string& sha256hash_hex,
      CertificateManagerPageHandler::DeleteCertificateCallback callback,
      bool confirmed) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    if (!confirmed) {
      std::move(callback).Run(nullptr);
      return;
    }

    scoped_refptr<net::X509Certificate> cert =
        cert_loader_->FindCertificate(sha256hash_hex);
    if (!cert) {
      // This error is not expected to be displayed under normal circumstances,
      // so it's not localized.
      std::move(callback).Run(
          certificate_manager::mojom::ActionResult::NewError("cert not found"));
      return;
    }

    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            &WritableClientCertSource::GetCertDBOnIOThread,
            NssServiceFactory::GetForContext(profile_)
                ->CreateNSSCertDatabaseGetterForIOThread(),
            base::BindOnce(
                &WritableClientCertSource::
                    GotNSSCertDatabaseForDeleteOnIOThread,
                cert, ClientCertManagementAccessControls(profile_),
                base::BindOnce(&WritableClientCertSource::FinishedDelete,
                               weak_ptr_factory_.GetWeakPtr(),
                               std::move(callback)))));
  }

  static void GotNSSCertDatabaseForDeleteOnIOThread(
      scoped_refptr<net::X509Certificate> cert,
      ClientCertManagementAccessControls client_cert_policy,
      base::OnceCallback<void(bool nss_delete_result)> finished_delete_callback,
      net::NSSCertDatabase* cert_db) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

    net::ScopedCERTCertificate nss_cert =
        net::x509_util::CreateCERTCertificateFromX509Certificate(cert.get());

    if (!nss_cert) {
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(finished_delete_callback), false));
      return;
    }

    const auto hardware_backed =
        cert_db->IsHardwareBacked(nss_cert.get())
            ? ClientCertManagementAccessControls::kHardwareBacked
            : ClientCertManagementAccessControls::kSoftwareBacked;
    const auto device_wide =
        ClientCertManagementAccessControls::kUser
        ;
    if (!client_cert_policy.IsChangeAllowed(hardware_backed, device_wide)) {
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE,
          base::BindOnce(std::move(finished_delete_callback), false));
      return;
    }

    cert_db->DeleteCertAndKeyAsync(
        std::move(nss_cert),
        base::BindPostTask(content::GetUIThreadTaskRunner({}),
                           std::move(finished_delete_callback)));
  }

  void FinishedDelete(
      CertificateManagerPageHandler::DeleteCertificateCallback callback,
      bool delete_result) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

    if (delete_result) {
      // Refresh the certificate list to remove the deleted cert, and
      // call the deletion complete callback once the list has been updated.
      cert_loader_->RefreshCachedCertificateList(base::BindOnce(
          std::move(callback),
          certificate_manager::mojom::ActionResult::NewSuccess(
              certificate_manager::mojom::SuccessResult::kSuccess)));
    } else {
      // TODO(crbug.com/40928765): pass through better error status codes from
      // the lower level deletion code?
      std::move(callback).Run(
          certificate_manager::mojom::ActionResult::NewError(
              l10n_util::GetStringUTF8(
                  IDS_SETTINGS_CERTIFICATE_MANAGER_V2_DELETE_ERROR)));
    }
  }

  std::unique_ptr<WritableCertLoader> cert_loader_;
  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;
  bool import_hardware_backed_;
  CertificateManagerPageHandler::ImportCertificateCallback import_callback_;
  raw_ptr<mojo::Remote<certificate_manager::mojom::CertificateManagerPage>>
      remote_client_;
  raw_ptr<Profile> profile_;
  base::WeakPtrFactory<WritableClientCertSource> weak_ptr_factory_{this};
};
#endif  // BUILDFLAG(IS_CHROMEOS)


}  // namespace

std::unique_ptr<CertificateManagerPageHandler::CertSource>
CreatePlatformClientCertSource(
    mojo::Remote<certificate_manager::mojom::CertificateManagerPage>*
        remote_client,
    Profile* profile) {
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)
  return std::make_unique<WritableClientCertSource>(remote_client, profile);
#else
  return std::make_unique<ClientCertSource>(
      CreatePlatformClientCertLoader(profile));
#endif
}

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
std::unique_ptr<CertificateManagerPageHandler::CertSource>
CreateProvisionedClientCertSource(Profile* profile) {
  return std::make_unique<ClientCertSource>(
      CreateProvisionedClientCertLoader(profile));
}
#endif

#if BUILDFLAG(IS_LINUX)
ClientCertManagementAccessControls::ClientCertManagementAccessControls(
    Profile* profile) {}

// The ClientCertificateManagementAllowed enterprise policy is ChromeOS only.
// There's no technical reason it couldn't be supported on Linux too, but
// unless someone asks for it, keeping the status quo is fine.
bool ClientCertManagementAccessControls::IsManagementAllowed(
    KeyStorage key_storage) const {
  return true;
}

bool ClientCertManagementAccessControls::IsChangeAllowed(
    KeyStorage key_storage,
    CertLocation cert_location) const {
  return true;
}
#endif  // BUILDFLAG(IS_LINUX)
