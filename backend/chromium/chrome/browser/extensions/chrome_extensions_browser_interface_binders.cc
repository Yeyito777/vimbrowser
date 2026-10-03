// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/chrome_extensions_browser_interface_binders.h"

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "build/branding_buildflags.h"
#include "build/chromeos_buildflags.h"
#include "components/guest_view/buildflags/buildflags.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/service_worker_version_base_info.h"
#include "extensions/buildflags/buildflags.h"
#include "extensions/common/extension.h"
#include "extensions/common/permissions/api_permission.h"
#include "extensions/common/permissions/permissions_data.h"
#include "mojo/public/cpp/bindings/binder_map.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/browser/api/mime_handler_private/mime_handler_private.h"
#include "extensions/browser/guest_view/mime_handler_view/mime_handler_view_guest.h"
#include "extensions/common/api/mime_handler.mojom.h"
#endif


static_assert(BUILDFLAG(ENABLE_EXTENSIONS_CORE));

namespace extensions {

namespace {

#if BUILDFLAG(ENABLE_EXTENSIONS)
void BindMimeHandlerService(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<mime_handler::MimeHandlerService> receiver) {
  auto* guest_view = MimeHandlerViewGuest::FromRenderFrameHost(frame_host);
  if (!guest_view) {
    return;
  }
  MimeHandlerServiceImpl::Create(guest_view->GetStreamWeakPtr(),
                                 std::move(receiver));
}

void BindBeforeUnloadControl(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<mime_handler::BeforeUnloadControl> receiver) {
  auto* guest_view = MimeHandlerViewGuest::FromRenderFrameHost(frame_host);
  if (!guest_view) {
    return;
  }
  guest_view->FuseBeforeUnloadControl(std::move(receiver));
}
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

}  // namespace

void PopulateChromeFrameBindersForExtension(
    mojo::BinderMapWithContext<content::RenderFrameHost*>* binder_map,
    content::RenderFrameHost* render_frame_host,
    const Extension* extension) {
  DCHECK(extension);


#if BUILDFLAG(ENABLE_EXTENSIONS)
  binder_map->Add<mime_handler::MimeHandlerService>(&BindMimeHandlerService);
  binder_map->Add<mime_handler::BeforeUnloadControl>(&BindBeforeUnloadControl);
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)
}

void PopulateChromeServiceWorkerBindersForExtension(
    mojo::BinderMapWithContext<const content::ServiceWorkerVersionBaseInfo&>*
        binder_map,
    content::BrowserContext* browser_context,
    const Extension* extension) {
}

}  // namespace extensions
