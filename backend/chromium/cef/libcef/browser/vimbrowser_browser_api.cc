// Copyright 2026 The vimbrowser Authors. All rights reserved.

#include <cstddef>
#include <cstdint>
#include <string>

#include "cef/include/internal/cef_export.h"
#include "cef/libcef/browser/browser_host_base.h"
#include "cef/libcef/browser/browser_platform_delegate.h"
#include "cef/libcef/browser/browser_platform_delegate_lookup.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "third_party/blink/public/mojom/frame/frame.mojom.h"

namespace {

using VimbrowserElementActivationCallback =
    void (*)(void* user_data, int result, int match_count);

}  // namespace

extern "C" CEF_EXPORT bool vimbrowser_browser_has_fps_sample(int browser_id) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate) {
    return false;
  }
  return delegate->HasFpsSample();
}

extern "C" CEF_EXPORT double vimbrowser_get_browser_fps(int browser_id) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate) {
    return 0.0;
  }
  return delegate->GetCurrentFps();
}

extern "C" CEF_EXPORT double vimbrowser_get_browser_refresh_rate(
    int browser_id) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate) {
    return 0.0;
  }
  return delegate->GetCompositorRefreshRate();
}

extern "C" CEF_EXPORT bool vimbrowser_browser_is_currently_audible(
    int browser_id) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate) {
    return false;
  }
  return delegate->IsCurrentlyAudible();
}

extern "C" CEF_EXPORT void vimbrowser_send_browser_command_key_event(
    int browser_id,
    const CefKeyEvent* event) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate || !event) {
    return;
  }
  delegate->SendVimbrowserBrowserCommandKeyEvent(*event);
}

extern "C" CEF_EXPORT bool vimbrowser_activate_element_by_selector(
    int browser_id,
    const char* selector,
    size_t selector_size,
    uint64_t* activation_nonce_high,
    uint64_t* activation_nonce_low,
    VimbrowserElementActivationCallback callback,
    void* user_data) {
  if (!selector || selector_size == 0 || !activation_nonce_high ||
      !activation_nonce_low || !callback) {
    return false;
  }

  auto browser = CefBrowserHostBase::GetBrowserForBrowserId(browser_id);
  content::WebContents* web_contents =
      browser ? browser->GetWebContents() : nullptr;
  auto* frame = web_contents
                    ? content::RenderFrameHostImpl::From(
                          web_contents->GetPrimaryMainFrame())
                    : nullptr;
  if (!frame || !frame->IsRenderFrameLive()) {
    return false;
  }

  const base::UnguessableToken activation_nonce =
      base::UnguessableToken::Create();
  *activation_nonce_high = activation_nonce.GetHighForSerialization();
  *activation_nonce_low = activation_nonce.GetLowForSerialization();

  auto result_callback = base::BindOnce(
      [](VimbrowserElementActivationCallback callback, void* user_data,
         blink::mojom::VimbrowserElementActivationResult result,
         uint32_t match_count) {
        callback(user_data, static_cast<int>(result),
                 static_cast<int>(match_count));
      },
      callback, user_data);
  frame->GetAssociatedLocalFrame()->VimbrowserActivateElement(
      std::string(selector, selector_size),
      frame->GetDocumentToken(), activation_nonce,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          std::move(result_callback),
          blink::mojom::VimbrowserElementActivationResult::kBackendUnavailable,
          0));
  return true;
}

extern "C" CEF_EXPORT bool
vimbrowser_get_current_file_dialog_activation_nonce(
    int browser_id,
    uint64_t* activation_nonce_high,
    uint64_t* activation_nonce_low) {
  if (!activation_nonce_high || !activation_nonce_low) {
    return false;
  }

  auto browser = CefBrowserHostBase::GetBrowserForBrowserId(browser_id);
  const std::optional<base::UnguessableToken> activation_nonce =
      browser ? browser->GetCurrentVimbrowserFileDialogActivationNonce()
              : std::nullopt;
  if (!activation_nonce) {
    return false;
  }

  *activation_nonce_high = activation_nonce->GetHighForSerialization();
  *activation_nonce_low = activation_nonce->GetLowForSerialization();
  return true;
}
