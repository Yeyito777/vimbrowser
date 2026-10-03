// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_CDM_DOCUMENT_SERVICE_IMPL_H_
#define CHROME_BROWSER_MEDIA_CDM_DOCUMENT_SERVICE_IMPL_H_

#include <set>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/document_service.h"
#include "media/mojo/mojom/cdm_document_service.mojom.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"


// Implements media::mojom::CdmDocumentService. Can only be used on the
// UI thread because PlatformVerificationFlow and the pref service lives on the
// UI thread.
// Ownership Note: There's one CdmDocumentServiceImpl per RenderFrame per
// service type ( MediaFoundationService or CdmService). For
// MediaFoundationService's case, this can be seen in the ownership chain of
// InterfaceFactoryImpl -> MediaFoundationCdmFactory -> MojoCdmHelper
// -> mojo::Remote<mojom::CdmDocumentService>.
class CdmDocumentServiceImpl final
    : public content::DocumentService<media::mojom::CdmDocumentService> {
 public:
  static void Create(
      content::RenderFrameHost* render_frame_host,
      mojo::PendingReceiver<media::mojom::CdmDocumentService> receiver);

  // media::mojom::CdmDocumentService implementation.
  void ChallengePlatform(const std::string& service_id,
                         const std::string& challenge,
                         ChallengePlatformCallback callback) final;
  void GetStorageId(uint32_t version, GetStorageIdCallback callback) final;

 private:
  CdmDocumentServiceImpl(
      content::RenderFrameHost& render_frame_host,
      mojo::PendingReceiver<media::mojom::CdmDocumentService> receiver);

  // |this| can only be destructed as a DocumentService.
  ~CdmDocumentServiceImpl() final;


  void OnStorageIdResponse(GetStorageIdCallback callback,
                           const std::vector<uint8_t>& storage_id);



  base::WeakPtrFactory<CdmDocumentServiceImpl> weak_factory_{this};
};

#endif  // CHROME_BROWSER_MEDIA_CDM_DOCUMENT_SERVICE_IMPL_H_
