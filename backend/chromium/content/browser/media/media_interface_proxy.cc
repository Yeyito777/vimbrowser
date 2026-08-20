// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/media/media_interface_proxy.h"

#include <map>
#include <memory>
#include <string>
#include <tuple>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/unguessable_token.h"
#include "build/build_config.h"
#include "content/browser/media/cdm_storage_common.h"
#include "content/browser/renderer_host/render_frame_host_delegate.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/public/browser/media_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/service_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/cdm_info.h"
#include "content/public/common/content_client.h"
#include "media/base/cdm_context.h"
#include "media/cdm/cdm_type.h"
#include "media/media_buildflags.h"
#include "media/mojo/buildflags.h"
#include "media/mojo/mojom/frame_interface_factory.mojom.h"
#include "media/mojo/mojom/media_service.mojom.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "mojo/public/cpp/bindings/remote_set.h"

#if BUILDFLAG(ENABLE_MOJO_CDM)
#include "content/public/browser/browser_context.h"
#include "content/public/browser/provision_fetcher_impl.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#endif

#if BUILDFLAG(ENABLE_LIBRARY_CDMS)
#include "base/feature_list.h"
#include "base/metrics/histogram_macros.h"
#include "content/browser/media/cdm_storage_manager.h"
#include "media/base/key_system_names.h"
#include "media/mojo/mojom/cdm_service.mojom.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#endif  // BUILDFLAG(ENABLE_LIBRARY_CDMS)

#if BUILDFLAG(IS_WIN) || BUILDFLAG(ENABLE_LIBRARY_CDMS)
#include "content/browser/media/cdm_registry_impl.h"
#include "content/browser/media/service_factory.h"
#include "media/base/media_switches.h"
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(ENABLE_LIBRARY_CDMS)



#if BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
#include "media/base/media_switches.h"
#include "mojo/public/cpp/bindings/message.h"
#endif  // BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)

namespace content {

namespace {

#if BUILDFLAG(ENABLE_LIBRARY_CDMS)

// The CDM name will be displayed as the process name in the Task Manager.
// Put a length limit and restrict to ASCII. Empty name is allowed, in which
// case the process name will be "media::mojom::CdmService".
bool IsValidCdmDisplayName(const std::string& cdm_name) {
  constexpr size_t kMaxCdmNameSize = 256;
  return cdm_name.size() <= kMaxCdmNameSize && base::IsStringASCII(cdm_name);
}

#endif  // BUILDFLAG(ENABLE_LIBRARY_CDMS)


// The amount of time to allow the secondary Media Service instance to idle
// before tearing it down. Only used if the Content embedder defines how to
// launch a secondary Media Service instance.
constexpr base::TimeDelta kSecondaryInstanceIdleTimeout = base::Seconds(5);

void MaybeLaunchSecondaryMediaService(
    mojo::Remote<media::mojom::MediaService>* remote) {
  *remote = GetContentClient()->browser()->RunSecondaryMediaService();
  if (*remote) {
    // If the embedder provides a secondary Media Service instance, it may run
    // out-of-process. Make sure we reset on disconnect to allow restart of
    // crashed instances, and reset on idle to allow for release of resources
    // when the service instance goes unused for a while.
    remote->reset_on_disconnect();
    remote->reset_on_idle_timeout(kSecondaryInstanceIdleTimeout);
  } else {
    // The embedder doesn't provide a secondary Media Service instance. Bind
    // permanently to a disconnected pipe which discards all calls.
    std::ignore = remote->BindNewPipeAndPassReceiver();
  }
}

// Returns a remote handle to the secondary Media Service instance, if the
// Content embedder defines how to create one. If not, this returns a non-null
// but non-functioning MediaService reference which discards all calls.
media::mojom::MediaService& GetSecondaryMediaService() {
  static base::NoDestructor<mojo::Remote<media::mojom::MediaService>> remote;
  if (!*remote)
    MaybeLaunchSecondaryMediaService(remote.get());
  return *remote->get();
}

class FrameInterfaceFactoryImpl : public media::mojom::FrameInterfaceFactory,
                                  public WebContentsObserver {
 public:
  FrameInterfaceFactoryImpl(RenderFrameHost* render_frame_host,
                            const media::CdmType& cdm_type)
      : WebContentsObserver(
            WebContents::FromRenderFrameHost(render_frame_host)),
        render_frame_host_(render_frame_host),
        cdm_type_(cdm_type) {}

  // media::mojom::FrameInterfaceFactory implementation:

  void CreateProvisionFetcher(
      mojo::PendingReceiver<media::mojom::ProvisionFetcher> receiver) override {
#if BUILDFLAG(ENABLE_MOJO_CDM)
    ProvisionFetcherImpl::Create(render_frame_host_->GetBrowserContext()
                                     ->GetDefaultStoragePartition()
                                     ->GetURLLoaderFactoryForBrowserProcess(),
                                 std::move(receiver));
#endif
  }

  void CreateCdmStorage(
      mojo::PendingReceiver<media::mojom::CdmStorage> receiver) override {
#if BUILDFLAG(ENABLE_LIBRARY_CDMS)
    if (cdm_type_.is_zero())
      return;

    auto storage_key =
        static_cast<RenderFrameHostImpl*>(render_frame_host_)->GetStorageKey();

    CdmStorageManager* cdm_storage_manager = static_cast<CdmStorageManager*>(
        render_frame_host_->GetStoragePartition()->GetCdmStorageDataModel());

    cdm_storage_manager->OpenCdmStorage(
        CdmStorageBindingContext(storage_key, cdm_type_), std::move(receiver));
#endif
  }


  void GetCdmOrigin(GetCdmOriginCallback callback) override {
    return std::move(callback).Run(
        render_frame_host_->GetLastCommittedOrigin());
  }

  void BindEmbedderReceiver(mojo::GenericPendingReceiver receiver) override {
    GetContentClient()->browser()->BindMediaServiceReceiver(
        render_frame_host_, std::move(receiver));
  }


 private:
  const raw_ptr<RenderFrameHost> render_frame_host_;
  const media::CdmType cdm_type_;

};

}  // namespace

MediaInterfaceProxy::MediaInterfaceProxy(RenderFrameHost* render_frame_host)
    : DocumentUserData(render_frame_host) {
  DVLOG(1) << __func__;

  media::CdmType cdm_type;

  auto frame_factory_getter = base::BindRepeating(
      &MediaInterfaceProxy::GetFrameServices, base::Unretained(this), cdm_type);
  media_interface_factory_ptr_ = std::make_unique<MediaInterfaceFactoryHolder>(
      base::BindRepeating(&GetMediaService), frame_factory_getter);
  secondary_interface_factory_ = std::make_unique<MediaInterfaceFactoryHolder>(
      base::BindRepeating(&GetSecondaryMediaService), frame_factory_getter);

  // |cdm_factory_map_| will be lazily connected in GetCdmFactory().
}

MediaInterfaceProxy::~MediaInterfaceProxy() {
  DVLOG(1) << __func__;
  DCHECK(thread_checker_.CalledOnValidThread());
}

void MediaInterfaceProxy::Bind(
    mojo::PendingReceiver<media::mojom::InterfaceFactory> receiver) {
  receivers_.Add(this, std::move(receiver));
}

void MediaInterfaceProxy::CreateAudioDecoder(
    mojo::PendingReceiver<media::mojom::AudioDecoder> receiver) {
  DCHECK(thread_checker_.CalledOnValidThread());
  InterfaceFactory* factory = media_interface_factory_ptr_->Get();
  if (factory)
    factory->CreateAudioDecoder(std::move(receiver));
}

void MediaInterfaceProxy::CreateVideoDecoder(
    mojo::PendingReceiver<media::mojom::VideoDecoder> receiver,
    mojo::PendingRemote<media::mojom::VideoDecoder> dst_video_decoder) {
  DCHECK(thread_checker_.CalledOnValidThread());
  // The browser process cannot act as a proxy for video decoding and clients
  // should not attempt to use it that way.
  DCHECK(!dst_video_decoder);

  InterfaceFactory* factory = media_interface_factory_ptr_->Get();
  if (!factory)
    return;

  mojo::PendingRemote<media::mojom::VideoDecoder> oop_video_decoder;
#if BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
  if (media::IsOutOfProcessVideoDecodingEnabled()) {
    render_frame_host().GetProcess()->CreateOOPVideoDecoder(
        oop_video_decoder.InitWithNewPipeAndPassReceiver());
  }
#endif  // BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
  factory->CreateVideoDecoder(std::move(receiver),
                              std::move(oop_video_decoder));
}

#if BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
void MediaInterfaceProxy::CreateVideoDecoderWithTracker(
    mojo::PendingReceiver<media::mojom::VideoDecoder> receiver,
    mojo::PendingRemote<media::mojom::VideoDecoderTracker> tracker) {
  // mojo::ReportBadMessage() should be called directly within the stack frame
  // of a message dispatch, hence the CHECK().
  // CreateVideoDecoderWithTracker() should be called by the browser process
  // only. This implementation is exposed to the renderer. Well-behaved clients
  // (renderers) shouldn't call CreateVideoDecoderWithTracker().
  CHECK(mojo::IsInMessageDispatch());
  mojo::ReportBadMessage("CreateVideoDecoderWithTracker() called unexpectedly");
}
#endif  // BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)

void MediaInterfaceProxy::CreateAudioEncoder(
    mojo::PendingReceiver<media::mojom::AudioEncoder> receiver) {
  DCHECK(thread_checker_.CalledOnValidThread());
  InterfaceFactory* factory = media_interface_factory_ptr_->Get();
  if (factory)
    factory->CreateAudioEncoder(std::move(receiver));
}

void MediaInterfaceProxy::CreateDefaultRenderer(
    const std::string& audio_device_id,
    mojo::PendingReceiver<media::mojom::Renderer> receiver) {
  DCHECK(thread_checker_.CalledOnValidThread());

  InterfaceFactory* factory = media_interface_factory_ptr_->Get();
  if (factory)
    factory->CreateDefaultRenderer(audio_device_id, std::move(receiver));
}

#if BUILDFLAG(ENABLE_CAST_RENDERER)
void MediaInterfaceProxy::CreateCastRenderer(
    const base::UnguessableToken& overlay_plane_id,
    mojo::PendingReceiver<media::mojom::Renderer> receiver) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // CastRenderer is always hosted in the secondary Media Service instance.
  // This may not be running in some test environments (e.g.
  // content_browsertests) even though renderers may still request to bind it.
  InterfaceFactory* factory = secondary_interface_factory_->Get();
  if (factory)
    factory->CreateCastRenderer(overlay_plane_id, std::move(receiver));
}
#endif



void MediaInterfaceProxy::CreateCdm(const media::CdmConfig& cdm_config,
                                    CreateCdmCallback create_cdm_cb) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DVLOG(1) << __func__ << ": cdm_config=" << cdm_config;

  // The remote process may drop the callback (e.g. in case of crash, or CDM
  // loading/initialization failure). Doing it here instead of in the renderer
  // process because the browser is trusted.
  auto callback = mojo::WrapCallbackWithDefaultInvokeIfNotRun(
      std::move(create_cdm_cb), mojo::NullRemote(), nullptr,
      media::CreateCdmStatus::kDisconnectionError);

  // Handle `use_hw_secure_codecs` cases first.
#if BUILDFLAG(USE_CHROMEOS_PROTECTED_MEDIA)
  bool enable_cdm_factory_daemon = true;
#if defined(ARCH_CPU_ARM_FAMILY)
  if (!base::FeatureList::IsEnabled(media::kEnableArmHwdrm)) {
    enable_cdm_factory_daemon = false;
  }
#endif  // defined(ARCH_CPU_ARM_FAMILY)
  if (enable_cdm_factory_daemon && cdm_config.use_hw_secure_codecs &&
      cdm_config.allow_distinctive_identifier) {
    auto* factory = media_interface_factory_ptr_->Get();
    if (factory) {
      // We need to intercept the callback in this case so we can fallback to
      // the library CDM in case of failure.
      factory->CreateCdm(
          cdm_config, base::BindOnce(&MediaInterfaceProxy::OnChromeOsCdmCreated,
                                     weak_factory_.GetWeakPtr(), cdm_config,
                                     std::move(callback)));
      return;
    }
  }
  // Fallback to use library CDM below.
  ReportCdmTypeUMA(CrosCdmType::kChromeCdm);
#endif  // BUILDFLAG(USE_CHROMEOS_PROTECTED_MEDIA)

#if BUILDFLAG(ENABLE_LIBRARY_CDMS)
  // Fallback to use CdmFactory even if `use_hw_secure_codecs` is true.
  auto* factory = GetCdmFactory(cdm_config.key_system);
#elif BUILDFLAG(ENABLE_CAST_RENDERER)
  // CDM service lives together with renderer service if cast renderer is
  // enabled, because cast renderer creates its own audio/video decoder. Note
  // that in content_browsertests (and Content Shell in general) we don't have
  // an a cast renderer and this interface will be unbound.
  auto* factory = secondary_interface_factory_->Get();
#else
  // CDM service lives together with audio/video decoder service.
  auto* factory = media_interface_factory_ptr_->Get();
#endif  // BUILDFLAG(ENABLE_LIBRARY_CDMS)

  if (!factory) {
    std::move(callback).Run(mojo::NullRemote(), nullptr,
                            media::CreateCdmStatus::kCdmFactoryCreationFailed);
    return;
  }

  factory->CreateCdm(cdm_config, std::move(callback));
}

mojo::PendingRemote<media::mojom::FrameInterfaceFactory>
MediaInterfaceProxy::GetFrameServices(const media::CdmType& cdm_type) {
  mojo::PendingRemote<media::mojom::FrameInterfaceFactory> factory;
  frame_factories_.Add(
      std::make_unique<FrameInterfaceFactoryImpl>(
          static_cast<RenderFrameHostImpl*>(&render_frame_host()), cdm_type),
      factory.InitWithNewPipeAndPassReceiver());
  return factory;
}


#if BUILDFLAG(ENABLE_LIBRARY_CDMS)

media::mojom::CdmFactory* MediaInterfaceProxy::GetCdmFactory(
    const std::string& key_system) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // CdmService only supports software secure codecs.
  auto cdm_info = CdmRegistryImpl::GetInstance()->GetCdmInfo(
      key_system, CdmInfo::Robustness::kSoftwareSecure);
  if (!cdm_info) {
    DLOG(ERROR) << "No valid CdmInfo for " << key_system;
    return nullptr;
  }

  if (cdm_info->path.empty()) {
    NOTREACHED() << "CDM path for " << key_system << " is empty";
  }
  if (!IsValidCdmDisplayName(cdm_info->name)) {
    NOTREACHED() << "Invalid CDM display name " << cdm_info->name;
  }

  auto& cdm_type = cdm_info->type;

  auto found = cdm_factory_map_.find(cdm_type);
  if (found != cdm_factory_map_.end())
    return found->second.get();

  return ConnectToCdmService(*cdm_info);
}

media::mojom::CdmFactory* MediaInterfaceProxy::ConnectToCdmService(
    const CdmInfo& cdm_info) {
  DVLOG(1) << __func__ << ": cdm_name = " << cdm_info.name;

  DCHECK(!cdm_factory_map_.count(cdm_info.type));

  auto* browser_context = render_frame_host().GetBrowserContext();
  auto& site = render_frame_host().GetSiteInstance()->GetSiteURL();
  auto& cdm_service = GetCdmService(browser_context, site, cdm_info);

  mojo::Remote<media::mojom::CdmFactory> cdm_factory_remote;
  cdm_service.CreateCdmFactory(cdm_factory_remote.BindNewPipeAndPassReceiver(),
                               GetFrameServices(cdm_info.type));
  cdm_factory_remote.set_disconnect_handler(
      base::BindOnce(&MediaInterfaceProxy::OnCdmServiceConnectionError,
                     base::Unretained(this), cdm_info.type));

  auto* cdm_factory = cdm_factory_remote.get();
  cdm_factory_map_.emplace(cdm_info.type, std::move(cdm_factory_remote));
  return cdm_factory;
}

void MediaInterfaceProxy::OnCdmServiceConnectionError(
    const media::CdmType& cdm_type) {
  DVLOG(1) << __func__;
  DCHECK(thread_checker_.CalledOnValidThread());

  DCHECK(cdm_factory_map_.count(cdm_type));
  cdm_factory_map_.erase(cdm_type);
}
#endif  // BUILDFLAG(ENABLE_LIBRARY_CDMS)


DOCUMENT_USER_DATA_KEY_IMPL(MediaInterfaceProxy);

}  // namespace content
