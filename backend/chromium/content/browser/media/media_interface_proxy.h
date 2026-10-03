// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_MEDIA_MEDIA_INTERFACE_PROXY_H_
#define CONTENT_BROWSER_MEDIA_MEDIA_INTERFACE_PROXY_H_

#include <map>
#include <memory>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "base/unguessable_token.h"
#include "build/build_config.h"
#include "content/browser/media/media_interface_factory_holder.h"
#include "content/public/browser/document_user_data.h"
#include "content/public/common/cdm_info.h"
#include "media/cdm/cdm_type.h"
#include "media/media_buildflags.h"
#include "media/mojo/buildflags.h"
#include "media/mojo/mojom/content_decryption_module.mojom.h"
#include "media/mojo/mojom/decryptor.mojom.h"
#include "media/mojo/mojom/interface_factory.mojom.h"
#include "media/mojo/services/media_service.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/unique_receiver_set.h"
#include "services/service_manager/public/mojom/interface_provider.mojom.h"


namespace content {

class RenderFrameHost;

// This implements the media::mojom::InterfaceFactory interface for a
// RenderFrameHostImpl to help create remote media components in different
// processes.
class MediaInterfaceProxy final : public DocumentUserData<MediaInterfaceProxy>,
                                  public media::mojom::InterfaceFactory {
 public:
  ~MediaInterfaceProxy() final;

  void Bind(mojo::PendingReceiver<media::mojom::InterfaceFactory> receiver);

  // media::mojom::InterfaceFactory implementation.
  void CreateAudioDecoder(
      mojo::PendingReceiver<media::mojom::AudioDecoder> receiver) final;
  void CreateVideoDecoder(
      mojo::PendingReceiver<media::mojom::VideoDecoder> receiver,
      mojo::PendingRemote<media::mojom::VideoDecoder> dst_video_decoder) final;
#if BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
  void CreateVideoDecoderWithTracker(
      mojo::PendingReceiver<media::mojom::VideoDecoder> receiver,
      mojo::PendingRemote<media::mojom::VideoDecoderTracker> tracker) final;
#endif  // BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
  void CreateAudioEncoder(
      mojo::PendingReceiver<media::mojom::AudioEncoder> receiver) final;
  void CreateDefaultRenderer(
      const std::string& audio_device_id,
      mojo::PendingReceiver<media::mojom::Renderer> receiver) final;
#if BUILDFLAG(ENABLE_CAST_RENDERER)
  void CreateCastRenderer(
      const base::UnguessableToken& overlay_plane_id,
      mojo::PendingReceiver<media::mojom::Renderer> receiver) final;
#endif
  void CreateCdm(const media::CdmConfig& cdm_config,
                 CreateCdmCallback create_cdm_cb) final;

 private:
  friend class DocumentUserData<MediaInterfaceProxy>;
  explicit MediaInterfaceProxy(RenderFrameHost* rfh);
  DOCUMENT_USER_DATA_KEY_DECL();

  // Gets services provided by the browser (at RenderFrameHost level) to the
  // mojo media (or CDM) service running remotely. |cdm_type| is used to
  // register the appropriate CdmStorage interface needed by the CDM. If
  // |cdm_type| is empty, CdmStorage interface won't be available.
  mojo::PendingRemote<media::mojom::FrameInterfaceFactory> GetFrameServices(
      const media::CdmType& cdm_type);

#if BUILDFLAG(ENABLE_LIBRARY_CDMS)
  // Gets a CdmFactory pointer for |key_system|. Returns null if unexpected
  // error happened.
  media::mojom::CdmFactory* GetCdmFactory(const std::string& key_system);

  // Connects to the CDM service associated with |cdm_info|, adds the new
  // CdmFactoryPtr to the |cdm_factory_map_|, and returns the newly created
  // CdmFactory pointer. Returns nullptr if unexpected error happened.
  media::mojom::CdmFactory* ConnectToCdmService(const CdmInfo& cdm_info);

  // Callback for connection error from the CdmFactoryPtr in the
  // |cdm_factory_map_| associated with |cdm_type|.
  void OnCdmServiceConnectionError(const media::CdmType& cdm_type);
#endif  // BUILDFLAG(ENABLE_LIBRARY_CDMS)



  mojo::UniqueReceiverSet<media::mojom::FrameInterfaceFactory> frame_factories_;

  // InterfacePtr to the remote InterfaceFactory implementation in the Media
  // Service hosted in the process specified by the "mojo_media_host" gn
  // argument. Available options are browser or GPU processes.
  std::unique_ptr<MediaInterfaceFactoryHolder> media_interface_factory_ptr_;

  // An interface factory bound to a secondary instance of the Media Service,
  // initialized only if the embedder provides an implementation of
  // |ContentBrowserClient::RunSecondaryMediaService()|. This is used to bind to
  // CDM interfaces as well as Cast-specific Renderer interfaces when available.
  std::unique_ptr<MediaInterfaceFactoryHolder> secondary_interface_factory_;

#if BUILDFLAG(ENABLE_LIBRARY_CDMS)
  // CDM type to CDM InterfaceFactory Remotes mapping, where the
  // InterfaceFactory instances live in the standalone CdmService instances.
  // These map entries effectively own the corresponding CDM processes.
  // Only using the CDM type to identify the CdmFactory is sufficient because
  // the BrowserContext and Site URL should never change.
  std::map<media::CdmType, mojo::Remote<media::mojom::CdmFactory>>
      cdm_factory_map_;
#endif  // BUILDFLAG(ENABLE_LIBRARY_CDMS)

  base::ThreadChecker thread_checker_;

  // Receivers for incoming interface requests from the the RenderFrameImpl.
  mojo::ReceiverSet<media::mojom::InterfaceFactory> receivers_;

  base::WeakPtrFactory<MediaInterfaceProxy> weak_factory_{this};
};

}  // namespace content

#endif  // CONTENT_BROWSER_MEDIA_MEDIA_INTERFACE_PROXY_H_
