// Copyright 2026 The vimbrowser Authors. All rights reserved.

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "cef/include/internal/cef_export.h"
#include "cef/include/cef_browser.h"
#include "cef/libcef/browser/browser_host_base.h"
#include "cef/libcef/browser/browser_platform_delegate.h"
#include "cef/libcef/browser/browser_platform_delegate_lookup.h"
#include "cef/libcef/browser/frame_host_impl.h"
#include "components/input/render_widget_host_input_event_router.h"
#include "content/browser/renderer_host/render_widget_host_view_base.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "third_party/blink/public/mojom/frame/frame.mojom.h"

namespace {

using VimbrowserElementActivationCallback =
    void (*)(void* user_data, int result, int match_count);
using VimbrowserControlInspectionCallback =
    void (*)(void* user_data, int result, const char* json, size_t json_size);

constexpr base::TimeDelta kVimbrowserBackendDeadline =
    base::Milliseconds(2500);

struct VimbrowserActivationCompletion {
  VimbrowserElementActivationCallback callback = nullptr;
  void* user_data = nullptr;
  bool completed = false;
};

void FinishActivation(
    const std::shared_ptr<VimbrowserActivationCompletion>& completion,
    blink::mojom::VimbrowserElementActivationResult result,
    int match_count) {
  if (!completion || completion->completed) {
    return;
  }
  completion->completed = true;
  VimbrowserElementActivationCallback callback = completion->callback;
  void* user_data = completion->user_data;
  completion->callback = nullptr;
  completion->user_data = nullptr;
  callback(user_data, static_cast<int>(result), match_count);
}

struct VimbrowserInspectionState {
  CefRefPtr<CefBrowserHostBase> browser;
  content::GlobalRenderFrameHostToken frame_token;
  blink::DocumentToken document_token;
  VimbrowserControlInspectionCallback callback = nullptr;
  void* user_data = nullptr;
  bool completed = false;
};

void FinishInspection(
    const std::shared_ptr<VimbrowserInspectionState>& state,
    blink::mojom::VimbrowserControlInspectionResult result,
    std::string json) {
  if (!state || state->completed) {
    return;
  }
  state->completed = true;
  VimbrowserControlInspectionCallback callback = state->callback;
  void* user_data = state->user_data;
  state->callback = nullptr;
  state->user_data = nullptr;
  state->browser = nullptr;
  callback(user_data, static_cast<int>(result), json.data(), json.size());
}

void ScheduleInspectionDeadline(
    const std::shared_ptr<VimbrowserInspectionState>& state) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](std::shared_ptr<VimbrowserInspectionState> state) {
            FinishInspection(
                state,
                blink::mojom::VimbrowserControlInspectionResult::
                    kBackendUnavailable,
                "{}");
          },
          state),
      kVimbrowserBackendDeadline);
}

content::RenderFrameHostImpl* ResolveFrame(
    CefRefPtr<CefBrowserHostBase> browser,
    const char* frame_identifier,
    size_t frame_identifier_size) {
  if (!browser || !frame_identifier || frame_identifier_size == 0 ||
      frame_identifier_size > 256) {
    return nullptr;
  }
  CefRefPtr<CefFrame> cef_frame = browser->GetFrameByIdentifier(
      std::string(frame_identifier, frame_identifier_size));
  auto* frame_impl =
      cef_frame ? static_cast<CefFrameHostImpl*>(cef_frame.get()) : nullptr;
  content::RenderFrameHost* frame =
      frame_impl ? frame_impl->GetRenderFrameHost() : nullptr;
  content::WebContents* web_contents = browser->GetWebContents();
  if (!frame || !web_contents || !frame->IsRenderFrameLive() ||
      !frame->IsActive() ||
      content::WebContents::FromRenderFrameHost(frame) != web_contents ||
      frame->GetOutermostMainFrame() != web_contents->GetPrimaryMainFrame()) {
    return nullptr;
  }
  return content::RenderFrameHostImpl::From(frame);
}

content::RenderFrameHostImpl* ResolveHandleFrame(
    CefRefPtr<CefBrowserHostBase> browser,
    const CefBrowserHostBase::VimbrowserElementHandle& handle) {
  content::RenderFrameHost* frame =
      content::RenderFrameHost::FromFrameToken(handle.frame_token);
  auto* frame_impl = content::RenderFrameHostImpl::From(frame);
  content::WebContents* web_contents = browser ? browser->GetWebContents() : nullptr;
  if (!frame_impl || !web_contents || !frame_impl->IsRenderFrameLive() ||
      !frame_impl->IsActive() ||
      frame_impl->GetDocumentToken() != handle.document_token ||
      content::WebContents::FromRenderFrameHost(frame_impl) != web_contents ||
      frame_impl->GetOutermostMainFrame() !=
          web_contents->GetPrimaryMainFrame()) {
    return nullptr;
  }
  return frame_impl;
}

struct VimbrowserHandleActivationState {
  CefRefPtr<CefBrowserHostBase> browser;
  CefBrowserHostBase::VimbrowserElementHandle handle;
  base::UnguessableToken activation_nonce;
  std::shared_ptr<VimbrowserActivationCompletion> completion;
};

void FinishHandleActivation(
    const std::shared_ptr<VimbrowserHandleActivationState>& state,
    blink::mojom::VimbrowserElementActivationResult result) {
  if (!state || !state->completion || state->completion->completed) {
    return;
  }
  state->browser = nullptr;
  FinishActivation(
      state->completion, result,
      result == blink::mojom::VimbrowserElementActivationResult::kDispatched
          ? 1
          : 0);
}

void ScheduleHandleActivationDeadline(
    const std::shared_ptr<VimbrowserHandleActivationState>& state) {
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](std::shared_ptr<VimbrowserHandleActivationState> state) {
            FinishHandleActivation(
                state,
                blink::mojom::VimbrowserElementActivationResult::
                    kBackendUnavailable);
          },
          state),
      kVimbrowserBackendDeadline);
}

content::RenderFrameHostImpl* LocalRootForFrame(
    content::RenderFrameHostImpl* frame) {
  content::RenderFrameHostImpl* current = frame;
  while (current && !current->is_local_root()) {
    current = content::RenderFrameHostImpl::From(current->GetParent());
  }
  return current;
}

void ActivatePreparedHandle(
    const std::shared_ptr<VimbrowserHandleActivationState>& state,
    const gfx::PointF& expected_point,
    blink::mojom::VimbrowserElementActivationResult validation) {
  using Result = blink::mojom::VimbrowserElementActivationResult;
  if (!state || !state->completion || state->completion->completed) {
    return;
  }
  if (validation != Result::kDispatched) {
    FinishHandleActivation(state, validation);
    return;
  }
  content::RenderFrameHostImpl* frame =
      ResolveHandleFrame(state->browser, state->handle);
  if (!frame) {
    FinishHandleActivation(state, Result::kStaleFrame);
    return;
  }
  auto callback = base::BindOnce(
      [](std::shared_ptr<VimbrowserHandleActivationState> state,
         Result result) { FinishHandleActivation(state, result); },
      state);
  frame->GetAssociatedLocalFrame()->VimbrowserActivatePreparedElement(
      state->handle.document_token, state->handle.dom_node_id, expected_point,
      state->activation_nonce,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          std::move(callback), Result::kBackendUnavailable));
}

void DispatchPreparedHandle(
    const std::shared_ptr<VimbrowserHandleActivationState>& state,
    const gfx::PointF& expected_point,
    base::WeakPtr<input::RenderWidgetHostViewInput> hit_view,
    std::optional<gfx::PointF> hit_point) {
  using Result = blink::mojom::VimbrowserElementActivationResult;
  if (!state || !state->completion || state->completion->completed) {
    return;
  }
  content::RenderFrameHostImpl* frame =
      ResolveHandleFrame(state->browser, state->handle);
  if (!frame) {
    FinishHandleActivation(state, Result::kStaleFrame);
    return;
  }
  auto* target_view = frame->GetView()
                          ? static_cast<content::RenderWidgetHostViewBase*>(
                                frame->GetView())
                          : nullptr;
  if (!target_view || !hit_view || hit_view.get() != target_view ||
      !hit_point ||
      std::hypot(hit_point->x() - expected_point.x(),
                 hit_point->y() - expected_point.y()) > 2.0f) {
    FinishHandleActivation(state, Result::kTargetObscured);
    return;
  }

  content::RenderFrameHostImpl* local_root = LocalRootForFrame(frame);
  if (!local_root) {
    FinishHandleActivation(state, Result::kStaleFrame);
    return;
  }
  if (local_root == frame) {
    ActivatePreparedHandle(state, expected_point, Result::kDispatched);
    return;
  }

  local_root->GetAssociatedLocalFrame()->
      VimbrowserValidateDescendantElementHit(
          local_root->GetDocumentToken(), frame->GetFrameToken(),
          state->handle.document_token, state->handle.dom_node_id,
          expected_point,
          mojo::WrapCallbackWithDefaultInvokeIfNotRun(
              base::BindOnce(&ActivatePreparedHandle, state, expected_point),
              Result::kBackendUnavailable));
}

void ContinueHandleActivation(
    const std::shared_ptr<VimbrowserHandleActivationState>& state,
    blink::mojom::VimbrowserElementActivationResult result,
    const gfx::PointF& point_in_local_root) {
  using Result = blink::mojom::VimbrowserElementActivationResult;
  if (!state || !state->completion || state->completion->completed) {
    return;
  }
  if (result != Result::kDispatched) {
    FinishHandleActivation(state, result);
    return;
  }
  content::RenderFrameHostImpl* frame =
      ResolveHandleFrame(state->browser, state->handle);
  content::WebContents* web_contents =
      state->browser ? state->browser->GetWebContents() : nullptr;
  auto* target_view = frame && frame->GetView()
                          ? static_cast<content::RenderWidgetHostViewBase*>(
                                frame->GetView())
                          : nullptr;
  auto* root_view = web_contents && web_contents->GetRenderWidgetHostView()
                        ? static_cast<content::RenderWidgetHostViewBase*>(
                              web_contents->GetRenderWidgetHostView())
                        : nullptr;
  auto* web_contents_impl =
      web_contents ? static_cast<content::WebContentsImpl*>(web_contents)
                   : nullptr;
  auto* router = web_contents_impl ? web_contents_impl->GetInputEventRouter()
                                   : nullptr;
  if (!frame || !target_view || !root_view || !router) {
    FinishHandleActivation(state, Result::kStaleFrame);
    return;
  }
  const gfx::PointF point_in_root =
      target_view->TransformPointToRootCoordSpaceF(point_in_local_root);
  router->GetRenderWidgetHostAtPointAsynchronously(
      root_view, point_in_root,
      base::BindOnce(&DispatchPreparedHandle, state, point_in_local_root));
}

}  // namespace

bool CefBrowserHost::VimbrowserBrowserHasFpsSample(int browser_id) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate) {
    return false;
  }
  return delegate->HasFpsSample();
}

double CefBrowserHost::VimbrowserGetBrowserFps(int browser_id) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate) {
    return 0.0;
  }
  return delegate->GetCurrentFps();
}

double CefBrowserHost::VimbrowserGetBrowserRefreshRate(int browser_id) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate) {
    return 0.0;
  }
  return delegate->GetCompositorRefreshRate();
}

bool CefBrowserHost::VimbrowserBrowserIsCurrentlyAudible(int browser_id) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate) {
    return false;
  }
  return delegate->IsCurrentlyAudible();
}

void CefBrowserHost::VimbrowserSendBrowserCommandKeyEvent(
    int browser_id,
    const CefKeyEvent& event) {
  auto* delegate = cef::GetBrowserPlatformDelegateForBrowserId(browser_id);
  if (!delegate) {
    return;
  }
  delegate->SendVimbrowserBrowserCommandKeyEvent(event);
}

bool CefBrowserHost::VimbrowserActivateElementBySelector(
    int browser_id,
    const CefString& selector,
    uint64_t& activation_nonce_high,
    uint64_t& activation_nonce_low,
    CefRefPtr<CefVimbrowserElementActivationCallback> callback) {
  if (selector.empty() || !callback) {
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
  activation_nonce_high = activation_nonce.GetHighForSerialization();
  activation_nonce_low = activation_nonce.GetLowForSerialization();

  auto result_callback = base::BindOnce(
      [](CefRefPtr<CefVimbrowserElementActivationCallback> callback,
         blink::mojom::VimbrowserElementActivationResult result,
         uint32_t match_count) {
        callback->OnComplete(static_cast<int>(result),
                             static_cast<int>(match_count));
      },
      callback);
  frame->GetAssociatedLocalFrame()->VimbrowserActivateElement(
      selector.ToString(), frame->GetDocumentToken(), activation_nonce,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          std::move(result_callback),
          blink::mojom::VimbrowserElementActivationResult::kBackendUnavailable,
          0));
  return true;
}

extern "C" CEF_EXPORT bool vimbrowser_frame_is_out_of_process(
    int browser_id,
    const char* frame_identifier,
    size_t frame_identifier_size) {
  auto browser = CefBrowserHostBase::GetBrowserForBrowserId(browser_id);
  content::RenderFrameHostImpl* frame =
      ResolveFrame(browser, frame_identifier, frame_identifier_size);
  content::WebContents* web_contents = browser ? browser->GetWebContents() : nullptr;
  content::RenderFrameHost* main =
      web_contents ? web_contents->GetPrimaryMainFrame() : nullptr;
  return frame && main && frame->GetProcess()->GetID() != main->GetProcess()->GetID();
}

extern "C" CEF_EXPORT bool vimbrowser_inspect_frame_controls(
    int browser_id,
    const char* frame_identifier,
    size_t frame_identifier_size,
    const char* role,
    size_t role_size,
    const char* exact_name,
    size_t exact_name_size,
    const char* context_contains,
    size_t context_contains_size,
    uint32_t limit,
    VimbrowserControlInspectionCallback callback,
    void* user_data) {
  if (!callback || role_size > 128 || exact_name_size > 256 ||
      context_contains_size > 512 || (!role && role_size) ||
      (!exact_name && exact_name_size) ||
      (!context_contains && context_contains_size)) {
    return false;
  }
  auto browser = CefBrowserHostBase::GetBrowserForBrowserId(browser_id);
  content::RenderFrameHostImpl* frame =
      ResolveFrame(browser, frame_identifier, frame_identifier_size);
  if (!browser || !frame) {
    callback(user_data,
             static_cast<int>(
                 blink::mojom::VimbrowserControlInspectionResult::kDocumentUnavailable),
             "{}", 2);
    return true;
  }

  auto query = blink::mojom::VimbrowserControlQuery::New();
  query->role = role ? std::string(role, role_size) : std::string();
  query->exact_name =
      exact_name ? std::string(exact_name, exact_name_size) : std::string();
  query->context_contains = context_contains
                                ? std::string(context_contains,
                                              context_contains_size)
                                : std::string();
  query->limit = std::clamp<uint32_t>(limit, 1, 100);
  auto state = std::make_shared<VimbrowserInspectionState>();
  state->browser = browser;
  state->frame_token = frame->GetGlobalFrameToken();
  state->document_token = frame->GetDocumentToken();
  state->callback = callback;
  state->user_data = user_data;
  ScheduleInspectionDeadline(state);
  auto result_callback = base::BindOnce(
      [](std::shared_ptr<VimbrowserInspectionState> state,
         blink::mojom::VimbrowserControlInspectionResult result,
         std::vector<blink::mojom::VimbrowserControlInfoPtr> controls,
         uint32_t match_count,
         bool truncated) {
        if (!state || state->completed) {
          return;
        }
        base::DictValue body;
        body.Set("match_count", static_cast<int>(match_count));
        body.Set("returned_count", static_cast<int>(controls.size()));
        body.Set("truncated", truncated);
        body.Set("handle_ttl_ms", 15000);
        base::ListValue output_controls;
        if (result ==
            blink::mojom::VimbrowserControlInspectionResult::kSuccess) {
          for (const auto& control : controls) {
            base::DictValue item;
            item.Set("handle",
                     state->browser->RegisterVimbrowserElementHandle(
                         state->frame_token, state->document_token,
                         control->dom_node_id));
            item.Set("role", control->role);
            item.Set("name", control->name);
            item.Set("tag", control->tag);
            item.Set("type", control->type);
            item.Set("id", control->id);
            item.Set("text", control->text);
            item.Set("context", control->context);
            item.Set("disabled", control->disabled);
            item.Set("visible", true);
            output_controls.Append(std::move(item));
          }
        }
        body.Set("controls", std::move(output_controls));
        std::string json;
        base::JSONWriter::Write(body, &json);
        FinishInspection(state, result, std::move(json));
      },
      state);
  frame->GetAssociatedLocalFrame()->VimbrowserInspectControls(
      state->document_token, std::move(query),
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          std::move(result_callback),
          blink::mojom::VimbrowserControlInspectionResult::kBackendUnavailable,
          std::vector<blink::mojom::VimbrowserControlInfoPtr>(), 0, false));
  return true;
}

extern "C" CEF_EXPORT bool vimbrowser_activate_element_handle(
    int browser_id,
    const char* capability,
    size_t capability_size,
    uint64_t* activation_nonce_high,
    uint64_t* activation_nonce_low,
    VimbrowserElementActivationCallback callback,
    void* user_data) {
  using Result = blink::mojom::VimbrowserElementActivationResult;
  if (!capability || capability_size == 0 || capability_size > 128 ||
      !activation_nonce_high || !activation_nonce_low || !callback) {
    return false;
  }
  auto browser = CefBrowserHostBase::GetBrowserForBrowserId(browser_id);
  if (!browser) {
    return false;
  }
  CefBrowserHostBase::VimbrowserElementHandle handle;
  const auto consume = browser->ConsumeVimbrowserElementHandle(
      std::string(capability, capability_size), &handle);
  if (consume !=
      CefBrowserHostBase::VimbrowserElementHandleResult::kSuccess) {
    callback(user_data,
             static_cast<int>(
                 consume == CefBrowserHostBase::VimbrowserElementHandleResult::kExpired
                     ? Result::kExpiredHandle
                     : Result::kInvalidHandle),
             0);
    return true;
  }
  content::RenderFrameHostImpl* frame = ResolveHandleFrame(browser, handle);
  if (!frame) {
    callback(user_data, static_cast<int>(Result::kStaleFrame), 0);
    return true;
  }

  auto state = std::make_shared<VimbrowserHandleActivationState>();
  state->browser = browser;
  state->handle = std::move(handle);
  state->activation_nonce = base::UnguessableToken::Create();
  state->completion = std::make_shared<VimbrowserActivationCompletion>();
  state->completion->callback = callback;
  state->completion->user_data = user_data;
  *activation_nonce_high = state->activation_nonce.GetHighForSerialization();
  *activation_nonce_low = state->activation_nonce.GetLowForSerialization();
  ScheduleHandleActivationDeadline(state);
  auto prepare_callback = base::BindOnce(&ContinueHandleActivation, state);
  frame->GetAssociatedLocalFrame()->VimbrowserPrepareElementActivation(
      state->handle.document_token, state->handle.dom_node_id,
      mojo::WrapCallbackWithDefaultInvokeIfNotRun(
          std::move(prepare_callback), Result::kBackendUnavailable,
          gfx::PointF()));
  return true;
}

bool CefBrowserHost::VimbrowserGetCurrentFileDialogActivationNonce(
    int browser_id,
    uint64_t& activation_nonce_high,
    uint64_t& activation_nonce_low) {
  auto browser = CefBrowserHostBase::GetBrowserForBrowserId(browser_id);
  const std::optional<base::UnguessableToken> activation_nonce =
      browser ? browser->GetCurrentVimbrowserFileDialogActivationNonce()
              : std::nullopt;
  if (!activation_nonce) {
    return false;
  }

  activation_nonce_high = activation_nonce->GetHighForSerialization();
  activation_nonce_low = activation_nonce->GetLowForSerialization();
  return true;
}
