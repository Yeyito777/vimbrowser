// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROME_BROWSER_INTERFACE_BINDERS_WEBUI_PARTS_H_
#define CHROME_BROWSER_CHROME_BROWSER_INTERFACE_BINDERS_WEBUI_PARTS_H_

#include "build/build_config.h"

namespace content {
class RenderFrameHost;
class WebUIBrowserInterfaceBrokerRegistry;
}  // namespace content

namespace mojo {
template <typename>
class BinderMapWithContext;
}  // namespace mojo

namespace chrome::internal {

// There are for code guarded by a feature BUILDFLAG.
// e.g. BUILDFLAG(ENABLE_EXTENSIONS)
void PopulateChromeWebUIFrameBindersPartsFeatures(
    mojo::BinderMapWithContext<content::RenderFrameHost*>* map,
    content::RenderFrameHost* render_frame_host);
void PopulateChromeWebUIFrameInterfaceBrokersUntrustedPartsFeatures(
    content::WebUIBrowserInterfaceBrokerRegistry& registry);

// These assumes "Desktop" is non-Android.
void PopulateChromeWebUIFrameBindersPartsDesktop(
    mojo::BinderMapWithContext<content::RenderFrameHost*>* map,
    content::RenderFrameHost* render_frame_host);
void PopulateChromeWebUIFrameInterfaceBrokersTrustedPartsDesktop(
    content::WebUIBrowserInterfaceBrokerRegistry& registry);
void PopulateChromeWebUIFrameInterfaceBrokersUntrustedPartsDesktop(
    content::WebUIBrowserInterfaceBrokerRegistry& registry);


}  // namespace chrome::internal

#endif  // CHROME_BROWSER_CHROME_BROWSER_INTERFACE_BINDERS_WEBUI_PARTS_H_
