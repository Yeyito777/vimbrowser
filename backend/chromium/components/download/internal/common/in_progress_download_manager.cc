// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/download/public/common/in_progress_download_manager.h"

#include <optional>

#include "base/functional/bind.h"
#include "base/metrics/histogram_functions.h"
#include "base/strings/string_util.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "components/download/database/download_db_entry.h"
#include "components/download/database/download_db_impl.h"
#include "components/download/database/download_namespace.h"
#include "components/download/internal/common/download_db_cache.h"
#include "components/download/internal/common/resource_downloader.h"
#include "components/download/public/common/download_features.h"
#include "components/download/public/common/download_file.h"
#include "components/download/public/common/download_item_impl.h"
#include "components/download/public/common/download_start_observer.h"
#include "components/download/public/common/download_stats.h"
#include "components/download/public/common/download_task_runner.h"
#include "components/download/public/common/download_url_parameters.h"
#include "components/download/public/common/download_utils.h"
#include "components/download/public/common/input_stream.h"
#include "components/leveldb_proto/public/proto_database_provider.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "services/device/public/mojom/wake_lock_provider.mojom.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_response_head.mojom.h"


namespace download {

namespace {

std::unique_ptr<DownloadItemImpl> CreateDownloadItemImpl(
    DownloadItemImplDelegate* delegate,
    const DownloadDBEntry entry,
    std::unique_ptr<DownloadEntry> download_entry) {
  if (!entry.download_info)
    return nullptr;

  // DownloadDBEntry migrated from in-progress cache has negative Ids.
  if (entry.download_info->id < 0)
    return nullptr;

  std::optional<InProgressInfo> in_progress_info =
      entry.download_info->in_progress_info;
  if (!in_progress_info)
    return nullptr;

  return std::make_unique<DownloadItemImpl>(
      delegate, entry.download_info->guid, entry.download_info->id,
      in_progress_info->current_path, in_progress_info->target_path,
      in_progress_info->url_chain, in_progress_info->referrer_url,
      in_progress_info->serialized_embedder_download_data,
      in_progress_info->tab_url, in_progress_info->tab_referrer_url,
      std::nullopt, in_progress_info->mime_type,
      in_progress_info->original_mime_type, in_progress_info->start_time,
      in_progress_info->end_time, in_progress_info->etag,
      in_progress_info->last_modified, in_progress_info->received_bytes,
      in_progress_info->total_bytes, in_progress_info->auto_resume_count,
      in_progress_info->hash, in_progress_info->state,
      in_progress_info->danger_type, in_progress_info->interrupt_reason,
      in_progress_info->paused, in_progress_info->metered, false, base::Time(),
      in_progress_info->transient, in_progress_info->received_slices,
      in_progress_info->range_request_from, in_progress_info->range_request_to,
      std::move(download_entry));
}

void OnUrlDownloadHandlerCreated(
    UrlDownloadHandler::UniqueUrlDownloadHandlerPtr downloader,
    base::WeakPtr<InProgressDownloadManager> download_manager,
    const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner) {
  main_task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&UrlDownloadHandler::Delegate::OnUrlDownloadHandlerCreated,
                     download_manager, std::move(downloader)));
}

void BeginResourceDownload(
    std::unique_ptr<DownloadUrlParameters> params,
    std::unique_ptr<network::ResourceRequest> request,
    std::unique_ptr<network::PendingSharedURLLoaderFactory>
        pending_url_loader_factory,
    const URLSecurityPolicy& url_security_policy,
    bool is_new_download,
    base::WeakPtr<InProgressDownloadManager> download_manager,
    const std::string& serialized_embedder_download_data,
    const GURL& tab_url,
    const GURL& tab_referrer_url,
    mojo::PendingRemote<device::mojom::WakeLockProvider> wake_lock_provider,
    bool is_background_mode,
    const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner) {
  DCHECK(GetIOTaskRunner()->BelongsToCurrentThread());
  UrlDownloadHandler::UniqueUrlDownloadHandlerPtr downloader(
      ResourceDownloader::BeginDownload(
          download_manager, std::move(params), std::move(request),
          network::SharedURLLoaderFactory::Create(
              std::move(pending_url_loader_factory)),
          url_security_policy, serialized_embedder_download_data, tab_url,
          tab_referrer_url, is_new_download, false,
          std::move(wake_lock_provider), is_background_mode, main_task_runner)
          .release(),
      base::OnTaskRunnerDeleter(
          base::SingleThreadTaskRunner::GetCurrentDefault()));

  OnUrlDownloadHandlerCreated(std::move(downloader), download_manager,
                              main_task_runner);
}

void CreateDownloadHandlerForNavigation(
    base::WeakPtr<InProgressDownloadManager> download_manager,
    std::unique_ptr<network::ResourceRequest> resource_request,
    int render_process_id,
    int render_frame_id,
    const std::string& serialized_embedder_download_data,
    const GURL& tab_url,
    const GURL& tab_referrer_url,
    std::vector<GURL> url_chain,
    net::CertStatus cert_status,
    network::mojom::URLResponseHeadPtr response_head,
    mojo::ScopedDataPipeConsumerHandle response_body,
    network::mojom::URLLoaderClientEndpointsPtr url_loader_client_endpoints,
    std::unique_ptr<network::PendingSharedURLLoaderFactory>
        pending_url_loader_factory,
    const URLSecurityPolicy& url_security_policy,
    mojo::PendingRemote<device::mojom::WakeLockProvider> wake_lock_provider,
    const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner,
    bool is_transient) {
  DCHECK(GetIOTaskRunner()->BelongsToCurrentThread());

  ResourceDownloader::InterceptNavigationResponse(
      download_manager, std::move(resource_request), render_process_id,
      render_frame_id, serialized_embedder_download_data, tab_url,
      tab_referrer_url, std::move(url_chain), std::move(cert_status),
      std::move(response_head), std::move(response_body),
      std::move(url_loader_client_endpoints),
      network::SharedURLLoaderFactory::Create(
          std::move(pending_url_loader_factory)),
      url_security_policy, std::move(wake_lock_provider), main_task_runner,
      is_transient);
}


}  // namespace

bool InProgressDownloadManager::Delegate::InterceptDownload(
    const DownloadCreateInfo& download_create_info) {
  return false;
}

base::FilePath
InProgressDownloadManager::Delegate::GetDefaultDownloadDirectory() {
  return base::FilePath();
}

InProgressDownloadManager::InProgressDownloadManager(
    Delegate* delegate,
    const base::FilePath& in_progress_db_dir,
    leveldb_proto::ProtoDatabaseProvider* db_provider,
    const IsOriginSecureCallback& is_origin_secure_cb,
    const URLSecurityPolicy& url_security_policy,
    WakeLockProviderBinder wake_lock_provider_binder)
    : delegate_(delegate),
      file_factory_(new DownloadFileFactory()),
      download_start_observer_(nullptr),
      is_origin_secure_cb_(is_origin_secure_cb),
      url_security_policy_(url_security_policy),
      wake_lock_provider_binder_(std::move(wake_lock_provider_binder)) {
  Initialize(in_progress_db_dir, db_provider);
}

InProgressDownloadManager::~InProgressDownloadManager() = default;

void InProgressDownloadManager::OnUrlDownloadStarted(
    std::unique_ptr<DownloadCreateInfo> download_create_info,
    std::unique_ptr<InputStream> input_stream,
    URLLoaderFactoryProvider::URLLoaderFactoryProviderPtr
        url_loader_factory_provider,
    UrlDownloadHandlerID downloader,
    DownloadUrlParameters::OnStartedCallback callback) {
  // If a new download's GUID already exists, skip it.
  if (!download_create_info->guid.empty() &&
      download_create_info->is_new_download &&
      GetDownloadByGuid(download_create_info->guid)) {
    LOG(WARNING) << "A download with the same GUID already exists, the new "
                    "request is ignored.";
    return;
  }
  StartDownload(std::move(download_create_info), std::move(input_stream),
                std::move(url_loader_factory_provider),
                base::BindOnce(&InProgressDownloadManager::CancelUrlDownload,
                               weak_factory_.GetWeakPtr(), downloader),
                std::move(callback));
}

void InProgressDownloadManager::OnUrlDownloadStopped(
    UrlDownloadHandlerID downloader) {
  for (auto ptr = url_download_handlers_.begin();
       ptr != url_download_handlers_.end(); ++ptr) {
    if (reinterpret_cast<UrlDownloadHandlerID>(ptr->get()) == downloader) {
      url_download_handlers_.erase(ptr);
      return;
    }
  }
}

void InProgressDownloadManager::OnUrlDownloadHandlerCreated(
    UrlDownloadHandler::UniqueUrlDownloadHandlerPtr downloader) {
  if (downloader)
    url_download_handlers_.push_back(std::move(downloader));
}

void InProgressDownloadManager::DownloadUrl(
    std::unique_ptr<DownloadUrlParameters> params) {
  if (!CanDownload(params.get()))
    return;

  download::RecordDownloadCountWithSource(
      download::DownloadCountTypes::DOWNLOAD_TRIGGERED_COUNT,
      params->download_source());

  // Start the new download, the download should be saved to the file path
  // specifcied in the |params|.
  BeginDownload(std::move(params), url_loader_factory_->Clone(),
                true /* is_new_download */,
                std::string() /* serialized_embedder_download_data */,
                GURL() /* tab_url */, GURL() /* tab_referral_url */);
}

bool InProgressDownloadManager::CanDownload(DownloadUrlParameters* params) {
  if (!params->is_transient())
    return false;

  if (!url_loader_factory_)
    return false;

  if (params->require_safety_checks())
    return false;

  if (params->file_path().empty())
    return false;

  return true;
}

void InProgressDownloadManager::GetAllDownloads(
    SimpleDownloadManager::DownloadVector* downloads) {
  for (auto& item : in_progress_downloads_)
    downloads->push_back(item.get());
}

DownloadItem* InProgressDownloadManager::GetDownloadByGuid(
    const std::string& guid) {
  for (auto& item : in_progress_downloads_) {
    if (item->GetGuid() == guid)
      return item.get();
  }
  return nullptr;
}

void InProgressDownloadManager::BeginDownload(
    std::unique_ptr<DownloadUrlParameters> params,
    std::unique_ptr<network::PendingSharedURLLoaderFactory>
        pending_url_loader_factory,
    bool is_new_download,
    const std::string& serialized_embedder_download_data,
    const GURL& tab_url,
    const GURL& tab_referrer_url) {
  std::unique_ptr<network::ResourceRequest> request =
      CreateResourceRequest(params.get());
  mojo::PendingRemote<device::mojom::WakeLockProvider> wake_lock_provider;
  if (wake_lock_provider_binder_) {
    wake_lock_provider_binder_.Run(
        wake_lock_provider.InitWithNewPipeAndPassReceiver());
  }
  GetIOTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &BeginResourceDownload, std::move(params), std::move(request),
          std::move(pending_url_loader_factory), url_security_policy_,
          is_new_download, weak_factory_.GetWeakPtr(),
          serialized_embedder_download_data, tab_url, tab_referrer_url,
          std::move(wake_lock_provider), !delegate_ /* is_background_mode */,
          base::SingleThreadTaskRunner::GetCurrentDefault()));
}

void InProgressDownloadManager::InterceptDownloadFromNavigation(
    std::unique_ptr<network::ResourceRequest> resource_request,
    int render_process_id,
    int render_frame_id,
    const std::string& serialized_embedder_download_data,
    const GURL& tab_url,
    const GURL& tab_referrer_url,
    std::vector<GURL> url_chain,
    net::CertStatus cert_status,
    network::mojom::URLResponseHeadPtr response_head,
    mojo::ScopedDataPipeConsumerHandle response_body,
    network::mojom::URLLoaderClientEndpointsPtr url_loader_client_endpoints,
    std::unique_ptr<network::PendingSharedURLLoaderFactory>
        pending_url_loader_factory,
    bool is_transient) {
  mojo::PendingRemote<device::mojom::WakeLockProvider> wake_lock_provider;
  if (wake_lock_provider_binder_) {
    wake_lock_provider_binder_.Run(
        wake_lock_provider.InitWithNewPipeAndPassReceiver());
  }

  GetIOTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &CreateDownloadHandlerForNavigation, weak_factory_.GetWeakPtr(),
          std::move(resource_request), render_process_id, render_frame_id,
          serialized_embedder_download_data, tab_url, tab_referrer_url,
          std::move(url_chain), std::move(cert_status),
          std::move(response_head), std::move(response_body),
          std::move(url_loader_client_endpoints),
          std::move(pending_url_loader_factory), url_security_policy_,
          std::move(wake_lock_provider),
          base::SingleThreadTaskRunner::GetCurrentDefault(), is_transient));
}

void InProgressDownloadManager::Initialize(
    const base::FilePath& in_progress_db_dir,
    leveldb_proto::ProtoDatabaseProvider* db_provider) {
  std::unique_ptr<DownloadDB> download_db;

  if (in_progress_db_dir.empty()) {
    download_db = std::make_unique<DownloadDB>();
  } else {
    download_db = std::make_unique<DownloadDBImpl>(
        DownloadNamespace::NAMESPACE_BROWSER_DOWNLOAD, in_progress_db_dir,
        db_provider);
  }

  download_db_cache_ =
      std::make_unique<DownloadDBCache>(std::move(download_db));
  if (GetDownloadDBTaskRunnerForTesting()) {
    download_db_cache_->SetTimerTaskRunnerForTesting(
        GetDownloadDBTaskRunnerForTesting());
  }
  download_db_cache_->Initialize(base::BindOnce(
      &InProgressDownloadManager::OnDBInitialized, weak_factory_.GetWeakPtr()));
}

void InProgressDownloadManager::ShutDown() {
  url_download_handlers_.clear();
}

void InProgressDownloadManager::DetermineDownloadTarget(
    DownloadItemImpl* download,
    DownloadTargetCallback callback) {
  base::FilePath target_path = download->GetForcedFilePath().empty()
                                   ? download->GetTargetFilePath()
                                   : download->GetForcedFilePath();
  // For non-Android, the code below is only used by tests.
  DownloadTargetInfo target_info;
  target_info.target_path = target_path;
  target_info.intermediate_path =
      download->GetFullPath().empty() ? target_path : download->GetFullPath();
  target_info.danger_type = download->GetDangerType();
  target_info.insecure_download_status = download->GetInsecureDownloadStatus();

  std::move(callback).Run(std::move(target_info));
}

void InProgressDownloadManager::ResumeInterruptedDownload(
    std::unique_ptr<DownloadUrlParameters> params,
    const std::string& serialized_embedder_download_data) {
  if (!url_loader_factory_)
    return;

  BeginDownload(std::move(params), url_loader_factory_->Clone(), false,
                serialized_embedder_download_data, GURL(), GURL());
}

bool InProgressDownloadManager::ShouldOpenDownload(
    DownloadItemImpl* item,
    ShouldOpenDownloadCallback callback) {
  return true;
}

void InProgressDownloadManager::ReportBytesWasted(DownloadItemImpl* download) {
  download_db_cache_->OnDownloadUpdated(download);
}

void InProgressDownloadManager::RemoveInProgressDownload(
    const std::string& guid) {
  download_db_cache_->RemoveEntry(guid);
}

void InProgressDownloadManager::StartDownload(
    std::unique_ptr<DownloadCreateInfo> info,
    std::unique_ptr<InputStream> stream,
    URLLoaderFactoryProvider::URLLoaderFactoryProviderPtr
        url_loader_factory_provider,
    DownloadJob::CancelRequestCallback cancel_request_callback,
    DownloadUrlParameters::OnStartedCallback on_started) {
  DCHECK(info);

  if (info->is_new_download &&
      (info->result == DOWNLOAD_INTERRUPT_REASON_NONE ||
       info->result ==
           DOWNLOAD_INTERRUPT_REASON_SERVER_CROSS_ORIGIN_REDIRECT)) {
    if (delegate_ && delegate_->InterceptDownload(*info)) {
      if (cancel_request_callback)
        std::move(cancel_request_callback).Run(false);
      GetIOTaskRunner()->DeleteSoon(FROM_HERE, std::move(stream));
      return;
    }
  }

  // |stream| is only non-null if the download request was successful.
  DCHECK(
      (info->result == DOWNLOAD_INTERRUPT_REASON_NONE && !stream->IsEmpty()) ||
      (info->result != DOWNLOAD_INTERRUPT_REASON_NONE && stream->IsEmpty()));
  DVLOG(20) << __func__
            << "() result=" << DownloadInterruptReasonToString(info->result);


  // If the download cannot be found locally, ask |delegate_| to provide the
  // DownloadItem.
  if (delegate_ && !GetDownloadByGuid(info->guid)) {
    delegate_->StartDownloadItem(
        std::move(info), std::move(on_started),
        base::BindOnce(&InProgressDownloadManager::StartDownloadWithItem,
                       weak_factory_.GetWeakPtr(), std::move(stream),
                       std::move(url_loader_factory_provider),
                       std::move(cancel_request_callback)));
  } else {
    std::string guid = info->guid;
    if (info->is_new_download) {
      auto download = std::make_unique<DownloadItemImpl>(
          this, DownloadItem::kInvalidId, *info);
      OnNewDownloadCreated(download.get());
      guid = download->GetGuid();
      DCHECK(!guid.empty());
      in_progress_downloads_.push_back(std::move(download));
    }
    StartDownloadWithItem(
        std::move(stream), std::move(url_loader_factory_provider),
        std::move(cancel_request_callback), std::move(info),
        static_cast<DownloadItemImpl*>(GetDownloadByGuid(guid)),
        base::FilePath(), false);
  }
}

void InProgressDownloadManager::StartDownloadWithItem(
    std::unique_ptr<InputStream> stream,
    URLLoaderFactoryProvider::URLLoaderFactoryProviderPtr
        url_loader_factory_provider,
    DownloadJob::CancelRequestCallback cancel_request_callback,
    std::unique_ptr<DownloadCreateInfo> info,
    DownloadItemImpl* download,
    const base::FilePath& duplicate_download_file_path,
    bool should_persist_new_download) {
  if (!download) {
    // If the download is no longer known to the DownloadManager, then it was
    // removed after it was resumed. Ignore. If the download is cancelled
    // while resuming, then also ignore the request.
    if (cancel_request_callback)
      std::move(cancel_request_callback).Run(false);
    // The ByteStreamReader lives and dies on the download sequence.
    if (info->result == DOWNLOAD_INTERRUPT_REASON_NONE)
      GetIOTaskRunner()->DeleteSoon(FROM_HERE, std::move(stream));
    return;
  }

  base::FilePath default_download_directory;
  if (delegate_)
    default_download_directory = delegate_->GetDefaultDownloadDirectory();

  if (info->is_new_download && !should_persist_new_download)
    non_persistent_download_guids_.insert(download->GetGuid());
  // If the download is not persisted, don't notify |download_db_cache_|.
  if (!non_persistent_download_guids_.contains(download->GetGuid())) {
    download_db_cache_->AddOrReplaceEntry(
        CreateDownloadDBEntryFromItem(*download));
    download->RemoveObserver(download_db_cache_.get());
    download->AddObserver(download_db_cache_.get());
  }

  std::unique_ptr<DownloadFile> download_file;
  if (info->result == DOWNLOAD_INTERRUPT_REASON_NONE) {
    DCHECK(stream);
    download_file.reset(file_factory_->CreateFile(
        std::move(info->save_info), default_download_directory,
        std::move(stream), download->GetId(), duplicate_download_file_path,
        download->DestinationObserverAsWeakPtr()));
  }
  // It is important to leave info->save_info intact in the case of an interrupt
  // so that the DownloadItem can salvage what it can out of a failed
  // resumption attempt.

  download->Start(std::move(download_file), std::move(cancel_request_callback),
                  *info, std::move(url_loader_factory_provider));

  if (download_start_observer_)
    download_start_observer_->OnDownloadStarted(download);
}

void InProgressDownloadManager::OnDBInitialized(
    bool success,
    std::unique_ptr<std::vector<DownloadDBEntry>> entries) {
  OnDownloadNamesRetrieved(std::move(entries), nullptr);
}

void InProgressDownloadManager::OnDownloadNamesRetrieved(
    std::unique_ptr<std::vector<DownloadDBEntry>> entries,
    DisplayNames display_names) {
  std::set<uint32_t> download_ids;
  display_names_ = std::move(display_names);
  for (const auto& entry : *entries) {
    auto item = CreateDownloadItemImpl(
        this, entry, CreateDownloadEntryFromDownloadDBEntry(entry));
    if (!item)
      continue;
    uint32_t download_id = item->GetId();
    // Remove entries with duplicate ids.
    if (download_id != DownloadItem::kInvalidId &&
        download_ids.contains(download_id)) {
      RemoveInProgressDownload(item->GetGuid());
      continue;
    }
    item->AddObserver(download_db_cache_.get());
    OnNewDownloadCreated(item.get());
    in_progress_downloads_.emplace_back(std::move(item));
    download_ids.insert(download_id);
  }
  OnInitialized();
  OnDownloadsInitialized();
}

std::vector<std::unique_ptr<download::DownloadItemImpl>>
InProgressDownloadManager::TakeInProgressDownloads() {
  return std::move(in_progress_downloads_);
}

base::FilePath InProgressDownloadManager::GetDownloadDisplayName(
    const base::FilePath& path) {
  return base::FilePath();
}

void InProgressDownloadManager::OnAllInprogressDownloadsLoaded() {
  display_names_.reset();
}

void InProgressDownloadManager::SetDelegate(Delegate* delegate) {
  delegate_ = delegate;
  if (initialized_)
    OnDownloadsInitialized();
}

void InProgressDownloadManager::OnDownloadsInitialized() {
  if (delegate_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&InProgressDownloadManager::NotifyDownloadsInitialized,
                       weak_factory_.GetWeakPtr()));
  }
}

void InProgressDownloadManager::NotifyDownloadsInitialized() {
  if (delegate_)
    delegate_->OnDownloadsInitialized();
}

void InProgressDownloadManager::AddInProgressDownloadForTest(
    std::unique_ptr<download::DownloadItemImpl> download) {
  in_progress_downloads_.push_back(std::move(download));
  NotifyDownloadsInitialized();
}

void InProgressDownloadManager::CancelUrlDownload(
    UrlDownloadHandlerID downloader,
    bool user_cancel) {
  OnUrlDownloadStopped(reinterpret_cast<UrlDownloadHandlerID>(downloader));
}

}  // namespace download
