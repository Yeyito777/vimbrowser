// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/service/input/input_manager.h"

#include <variant>


#include <utility>

#include "base/memory/scoped_refptr.h"
#include "base/metrics/histogram_macros.h"
#include "base/task/common/task_annotator.h"
#include "components/viz/service/input/render_input_router_delegate_impl.h"
#include "components/viz/service/input/render_input_router_iterator_impl.h"
#include "components/viz/service/input/render_input_router_support_child_frame.h"


namespace viz {

namespace {


bool IsFrameMetadataAvailable(CompositorFrameSinkSupport* support) {
  return support && support->GetLastActivatedFrameMetadata();
}

}  // namespace

FrameSinkMetadata::FrameSinkMetadata(
    base::UnguessableToken grouping_id,
    std::unique_ptr<RenderInputRouterSupportBase> support,
    std::unique_ptr<RenderInputRouterDelegateImpl> delegate)
    : grouping_id(grouping_id),
      rir_support(std::move(support)),
      rir_delegate(std::move(delegate)) {}

FrameSinkMetadata::~FrameSinkMetadata() = default;

FrameSinkMetadata::FrameSinkMetadata(FrameSinkMetadata&& other) = default;
FrameSinkMetadata& FrameSinkMetadata::operator=(FrameSinkMetadata&& other) =
    default;

namespace {


}  // namespace

InputManager::~InputManager() {
  frame_sink_manager_->RemoveObserver(this);
}

InputManager::InputManager(FrameSinkManagerImpl* frame_sink_manager)
    :
      frame_sink_manager_(frame_sink_manager) {
  TRACE_EVENT("viz", "InputManager::InputManager");
  DCHECK(frame_sink_manager_);
  frame_sink_manager_->AddObserver(this);
}

std::unique_ptr<input::FlingSchedulerBase> InputManager::MakeFlingScheduler(
    input::RenderInputRouter* rir,
    const FrameSinkId& frame_sink_id) {
  NOTREACHED();
}

void InputManager::SetupRenderInputRouter(
    input::RenderInputRouter* render_input_router,
    const FrameSinkId& frame_sink_id,
    mojo::PendingRemote<blink::mojom::RenderInputRouterClient> rir_client,
    bool force_enable_zoom) {
  // TODO(382291983): Setup RenderInputRouter's mojo connections to renderer.
  render_input_router->SetFlingScheduler(
      MakeFlingScheduler(render_input_router, frame_sink_id));

  render_input_router->SetupInputRouter(
      GetDeviceScaleFactorForId(frame_sink_id));

  // The input router in Viz is always active.  The active state of renderer
  // input router determines if input would be transferred to Viz or not.  See
  // `RenderWidgetHostViewAndroid::OnTouchEvent`
  render_input_router->input_router()->MakeActive();

  render_input_router->SetForceEnableZoom(force_enable_zoom);
  render_input_router->BindRenderInputRouterInterfaces(std::move(rir_client));
  render_input_router->RendererWidgetCreated(/*for_frame_widget=*/true,
                                             /*is_in_viz=*/true);
}

void InputManager::OnCreateCompositorFrameSink(
    const FrameSinkId& frame_sink_id,
    bool is_root,
    input::mojom::RenderInputRouterConfigPtr render_input_router_config,
    bool create_input_receiver,
    gpu::SurfaceHandle surface_handle) {
  TRACE_EVENT("viz", "InputManager::OnCreateCompositorFrameSink",
              "config_is_null", !render_input_router_config, "frame_sink_id",
              frame_sink_id);
  if (is_root) {
    MaybeRecreateRootRenderInputRouterSupports(frame_sink_id);
  }

  // `render_input_router_config` is non null only when layer tree frame sinks
  // for renderer are being requested.
  if (!render_input_router_config) {
    return;
  }

  DCHECK(render_input_router_config->rir_client.is_valid());
  DCHECK(input::InputUtils::IsTransferInputToVizSupported() && !is_root);

  const base::UnguessableToken grouping_id =
      render_input_router_config->grouping_id;

  auto [it, inserted] = rwhier_map_.try_emplace(
      grouping_id,
      base::MakeRefCounted<input::RenderWidgetHostInputEventRouter>(
          frame_sink_manager_, this));

  if (inserted) {
    TRACE_EVENT_INSTANT("viz", "RenderWidgetHostInputEventRouterCreated",
                        "grouping_id", grouping_id.ToString());
  }

  // |rir_delegate| should outlive |render_input_router|.
  auto rir_delegate = std::make_unique<RenderInputRouterDelegateImpl>(
      it->second, *this, frame_sink_id);

  // Sets up RenderInputRouter.
  auto render_input_router = std::make_unique<input::RenderInputRouter>(
      /* host */ nullptr,
      /* fling_scheduler */ nullptr,
      /* delegate */ rir_delegate.get(),
      base::SingleThreadTaskRunner::GetCurrentDefault());
  SetupRenderInputRouter(render_input_router.get(), frame_sink_id,
                         std::move(render_input_router_config->rir_client),
                         render_input_router_config->force_enable_zoom);

  frame_sink_metadata_map_.emplace(std::make_pair(
      frame_sink_id,
      FrameSinkMetadata{grouping_id,
                        MakeRenderInputRouterSupport(render_input_router.get(),
                                                     frame_sink_id),
                        std::move(rir_delegate)}));

  rir_map_.emplace(
      std::make_pair(frame_sink_id, std::move(render_input_router)));
}

void InputManager::OnDestroyedCompositorFrameSink(
    const FrameSinkId& frame_sink_id) {
  TRACE_EVENT("viz", "InputManager::OnDestroyedCompositorFrameSink",
              "frame_sink_id", frame_sink_id);

  auto frame_sink_metadata_map_iter =
      frame_sink_metadata_map_.find(frame_sink_id);

  // Return early if |frame_sink_id| is associated with a non layer tree frame
  // sink.
  if (frame_sink_metadata_map_iter == frame_sink_metadata_map_.end()) {
    return;
  }

  // RenderInputRouterSupportBase must be destroyed first since it holds a
  // reference to RenderInputRouter, otherwise, it could lead to dangling
  // references.
  frame_sink_metadata_map_iter->second.rir_support.reset();

  auto rir_iter = rir_map_.find(frame_sink_id);
  CHECK(rir_iter != rir_map_.end());
  rir_map_.erase(rir_iter);

  base::UnguessableToken grouping_id =
      frame_sink_metadata_map_iter->second.grouping_id;
  // Deleting FrameSinkMetadata for |frame_sink_id| decreases the refcount for
  // RenderWidgetHostInputEventRouter in |rwhier_map_|(associated with the
  // RenderInputRouterDelegateImpl), for this |frame_sink_id|.
  frame_sink_metadata_map_.erase(frame_sink_metadata_map_iter);

  auto it = rwhier_map_.find(grouping_id);
  if (it != rwhier_map_.end()) {
    if (it->second->HasOneRef()) {
      // There are no CompositorFrameSinks associated with this
      // RenderWidgetHostInputEventRouter, delete it.
      rwhier_map_.erase(it);
    }
  }
}

void InputManager::OnRegisteredFrameSinkHierarchy(
    const FrameSinkId& parent_frame_sink_id,
    const FrameSinkId& child_frame_sink_id) {
  // Either the `child_frame_sink_id` corresponds to a layer tree frame sink, or
  // the OnCreateCompositorFrameSink call hasn't came in yet. We don't care
  // about the former case in InputManager, for the later correct construction
  // will take place when `OnCreateCompositorFrameSink` call will come.
  auto it = frame_sink_metadata_map_.find(child_frame_sink_id);
  if (it == frame_sink_metadata_map_.end()) {
    return;
  }

  const int num_parents =
      frame_sink_manager_->GetNumParents(child_frame_sink_id);
  if (num_parents > 1) {
    // Let UnregisterFrameSinkHierarchy do the reconstruction for this
    // RenderInputRouterSupport.
    return;
  }
  // `child_frame_sink_id` just got registered to `parent_frame_sink_id`,
  // `num_parents` should not be zero.
  CHECK_EQ(num_parents, 1);

  RecreateRenderInputRouterSupport(child_frame_sink_id,
                                   /* frame_sink_metadata= */ it->second);
}

void InputManager::OnUnregisteredFrameSinkHierarchy(
    const FrameSinkId& parent_frame_sink_id,
    const FrameSinkId& child_frame_sink_id) {
  auto it = frame_sink_metadata_map_.find(child_frame_sink_id);
  if (it == frame_sink_metadata_map_.end()) {
    return;
  }

  if (frame_sink_manager_->GetNumParents(child_frame_sink_id) != 1) {
    return;
  }

  RecreateRenderInputRouterSupport(child_frame_sink_id,
                                   /* frame_sink_metadata= */ it->second);
}

void InputManager::OnFrameSinkDeviceScaleFactorChanged(
    const FrameSinkId& frame_sink_id,
    float device_scale_factor) {
  auto rir_iter = rir_map_.find(frame_sink_id);
  // Return early if |frame_sink_id| is associated with a non layer tree frame
  // sink.
  if (rir_iter == rir_map_.end()) {
    return;
  }

  // Update device scale factor in RenderInputRouter from latest activated
  // compositor frame.
  rir_iter->second->SetDeviceScaleFactor(device_scale_factor);
}

void InputManager::OnFrameSinkMobileOptimizedChanged(
    const FrameSinkId& frame_sink_id,
    bool is_mobile_optimized) {
  auto rir_itr = rir_map_.find(frame_sink_id);
  if (rir_itr == rir_map_.end()) {
    return;
  }
  rir_itr->second->input_router()->NotifySiteIsMobileOptimized(
      is_mobile_optimized);

  auto metadata_itr = frame_sink_metadata_map_.find(frame_sink_id);
  CHECK(metadata_itr != frame_sink_metadata_map_.end());
  FrameSinkMetadata& frame_sink_metadata = metadata_itr->second;
  CHECK(frame_sink_metadata.is_mobile_optimized != is_mobile_optimized);
  frame_sink_metadata.is_mobile_optimized = is_mobile_optimized;
  frame_sink_metadata.rir_support->NotifySiteIsMobileOptimized(
      is_mobile_optimized);
}

input::TouchEmulator* InputManager::GetTouchEmulator(bool create_if_necessary) {
  return nullptr;
}

const DisplayHitTestQueryMap& InputManager::GetDisplayHitTestQuery() const {
  return frame_sink_manager_->GetDisplayHitTestQuery();
}

float InputManager::GetDeviceScaleFactorForId(
    const FrameSinkId& frame_sink_id) {
  auto* support = frame_sink_manager_->GetFrameSinkForId(frame_sink_id);
  CHECK(support);

  if (!IsFrameMetadataAvailable(support)) {
    // If a CompositorFrame hasn't been submitted yet for a child frame, we fall
    // back to use RootCompositorFrameSink's submitted frame metadata.
    support = frame_sink_manager_->GetFrameSinkForId(
        GetRootCompositorFrameSinkId(frame_sink_id));

    // If there's still no activated frame metadata available, return a default
    // scale factor of 1.0.
    if (!IsFrameMetadataAvailable(support)) {
      return 1.0;
    }
  }

  return support->GetLastActivatedFrameMetadata()->device_scale_factor;
}

FrameSinkId InputManager::GetRootCompositorFrameSinkId(
    const FrameSinkId& child_frame_sink_id) {
  return frame_sink_manager_->GetOldestRootCompositorFrameSinkId(
      child_frame_sink_id);
}

RenderInputRouterSupportBase* InputManager::GetParentRenderInputRouterSupport(
    const FrameSinkId& frame_sink_id) {
  auto parent_id =
      frame_sink_manager_->GetOldestParentByChildFrameId(frame_sink_id);

  CHECK(!frame_sink_manager_->IsFrameSinkIdInRootSinkMap(parent_id));

  auto it = frame_sink_metadata_map_.find(parent_id);
  if (it != frame_sink_metadata_map_.end()) {
    return it->second.rir_support.get();
  }
  return nullptr;
}

RenderInputRouterSupportBase* InputManager::GetRootRenderInputRouterSupport(
    const FrameSinkId& frame_sink_id) {
  auto parent_frame_sink_id =
      frame_sink_manager_->GetOldestParentByChildFrameId(frame_sink_id);
  FrameSinkId current_id = frame_sink_id;

  while (
      parent_frame_sink_id.is_valid() &&
      !frame_sink_manager_->IsFrameSinkIdInRootSinkMap(parent_frame_sink_id)) {
    current_id = parent_frame_sink_id;
    parent_frame_sink_id = frame_sink_manager_->GetOldestParentByChildFrameId(
        parent_frame_sink_id);
  }

  auto it = frame_sink_metadata_map_.find(current_id);
  if (it != frame_sink_metadata_map_.end() &&
      !it->second.rir_support->IsRenderInputRouterSupportChildFrame()) {
    return it->second.rir_support.get();
  }
  return nullptr;
}

const CompositorFrameMetadata* InputManager::GetLastActivatedFrameMetadata(
    const FrameSinkId& frame_sink_id) {
  auto* support = frame_sink_manager_->GetFrameSinkForId(frame_sink_id);
  if (!IsFrameMetadataAvailable(support)) {
    return nullptr;
  }
  return support->GetLastActivatedFrameMetadata();
}

std::unique_ptr<input::RenderInputRouterIterator>
InputManager::GetEmbeddedRenderInputRouters(const FrameSinkId& id) {
  auto rirs = std::make_unique<RenderInputRouterIteratorImpl>(
      *this, frame_sink_manager_->GetChildrenByParent(id));
  return std::move(rirs);
}

input::mojom::RenderInputRouterDelegateClient*
InputManager::GetRIRDelegateClientRemote(const FrameSinkId& frame_sink_id) {
  auto itr = rir_delegate_remote_map_.find(frame_sink_id);
  if (itr == rir_delegate_remote_map_.end()) {
    return nullptr;
  }
  return itr->second.get();
}

std::optional<bool> InputManager::IsDelegatedInkHovering(
    const FrameSinkId& frame_sink_id) {
  auto* support = frame_sink_manager_->GetFrameSinkForId(frame_sink_id);
  if (!IsFrameMetadataAvailable(support) ||
      !support->GetLastActivatedFrameMetadata()->delegated_ink_metadata) {
    return std::nullopt;
  }
  return support->GetLastActivatedFrameMetadata()
      ->delegated_ink_metadata->is_hovering();
}


void InputManager::StateOnTouchTransfer(
    input::mojom::TouchTransferStatePtr state) {
}

void InputManager::ForceEnableZoomStateChanged(
    bool force_enable_zoom,
    const FrameSinkId& frame_sink_id) {
  auto itr = rir_map_.find(frame_sink_id);
  if (itr != rir_map_.end()) {
    itr->second->SetForceEnableZoom(force_enable_zoom);
  }
}

void InputManager::StopFlingingOnViz(const FrameSinkId& frame_sink_id) {
  auto iter = frame_sink_metadata_map_.find(frame_sink_id);
  if (iter != frame_sink_metadata_map_.end()) {
    iter->second.rir_support->StopFlingingOnViz();
  }
}

void InputManager::RestartInputEventAckTimeoutIfNecessary(
    const FrameSinkId& frame_sink_id) {
  auto itr = rir_map_.find(frame_sink_id);
  if (itr == rir_map_.end()) {
    return;
  }
  itr->second->RestartInputEventAckTimeoutIfNecessary();
}

void InputManager::NotifyVisibilityChanged(const FrameSinkId& frame_sink_id,
                                           bool is_hidden) {
  auto itr = frame_sink_metadata_map_.find(frame_sink_id);
  if (itr == frame_sink_metadata_map_.end()) {
    return;
  }
  itr->second.rir_delegate->SetIsHidden(is_hidden);
}

void InputManager::ResetGestureDetection(
    const FrameSinkId& root_widget_frame_sink_id) {
}

void InputManager::SetupRendererInputRouterDelegateRegistry(
    mojo::PendingReceiver<mojom::RendererInputRouterDelegateRegistry>
        receiver) {
  TRACE_EVENT("viz", "InputManager::SetupRendererInputRouterDelegateRegistry");
  registry_receiver_.Bind(std::move(receiver));
}

void InputManager::SetupRenderInputRouterDelegateConnection(
    const FrameSinkId& frame_sink_id,
    mojo::PendingAssociatedRemote<input::mojom::RenderInputRouterDelegateClient>
        rir_delegate_remote,
    mojo::PendingAssociatedReceiver<input::mojom::RenderInputRouterDelegate>
        rir_delegate_receiver) {
  TRACE_EVENT("viz", "InputManager::SetupRenderInputRouterDelegateConnection");
  rir_delegate_remote_map_[frame_sink_id].Bind(std::move(rir_delegate_remote));
  rir_delegate_remote_map_[frame_sink_id].set_disconnect_handler(
      base::BindOnce(&InputManager::OnRIRDelegateClientDisconnected,
                     base::Unretained(this), frame_sink_id));

  rir_delegate_receivers_.Add(this, std::move(rir_delegate_receiver));
}

void InputManager::NotifyRendererBlockStateChanged(
    bool blocked,
    const std::vector<FrameSinkId>& rirs) {
  for (auto& frame_sink_id : rirs) {
    auto itr = rir_map_.find(frame_sink_id);
    if (itr == rir_map_.end()) {
      continue;
    }
    itr->second->RenderProcessBlockedStateChanged(blocked);
  }
}

GpuServiceImpl* InputManager::GetGpuService() {
  return frame_sink_manager_->GetGpuService();
}

input::RenderInputRouter* InputManager::GetRenderInputRouterFromFrameSinkId(
    const FrameSinkId& id) {
  auto itr = rir_map_.find(id);
  if (itr == rir_map_.end()) {
    return nullptr;
  }
  return itr->second.get();
}

bool InputManager::ReturnInputBackToBrowser() {

  // `ReturnInputBackToBrowser` is only being called from Android specific
  // usecases currently with InputVizard.
  NOTREACHED();
}

void InputManager::SetBeginFrameSource(const FrameSinkId& frame_sink_id,
                                       BeginFrameSource* begin_frame_source) {
  TRACE_EVENT("input", "InputManager::SetBeginFrameSource", "frame_sink_id",
              frame_sink_id);
  // Return early if |frame_sink_id| is associated with non layer tree frame
  // sink.
  auto itr = rir_map_.find(frame_sink_id);
  if (itr == rir_map_.end()) {
    return;
  }
  CHECK(itr->second.get());
  itr->second->SetBeginFrameSourceForFlingScheduler(begin_frame_source);
}

base::ReadOnlySharedMemoryRegion InputManager::DuplicateVizTouchStateRegion()
    const {
  // Return invalid region if not available.
  return base::ReadOnlySharedMemoryRegion();
}

void InputManager::MaybeRecreateRootRenderInputRouterSupports(
    const FrameSinkId& root_frame_sink_id) {
  TRACE_EVENT_INSTANT(
      "input", "InputManager::MaybeRecreateRootRenderInputRouterSupports");

  auto children = frame_sink_manager_->GetChildrenByParent(root_frame_sink_id);
  for (auto& frame_sink_id : children) {
    auto iter = frame_sink_metadata_map_.find(frame_sink_id);
    // Only attempt to recreate RenderInputRouterSupport for `frame_sink_id`
    // associated with layer tree frame sinks.
    if (iter != frame_sink_metadata_map_.end() &&
        iter->second.rir_support->IsRenderInputRouterSupportChildFrame()) {
      FrameSinkMetadata& metadata = iter->second;
      metadata.rir_support.reset();
      auto* rir = rir_map_.find(frame_sink_id)->second.get();
      metadata.rir_support = MakeRenderInputRouterSupport(rir, frame_sink_id);
      metadata.rir_support->NotifySiteIsMobileOptimized(
          metadata.is_mobile_optimized);
    }
  }
}

void InputManager::RecreateRenderInputRouterSupport(
    const FrameSinkId& child_frame_sink_id,
    FrameSinkMetadata& frame_sink_metadata) {
  auto rir_map_it = rir_map_.find(child_frame_sink_id);
  CHECK(rir_map_it != rir_map_.end());
  input::RenderInputRouter* rir = rir_map_it->second.get();

  frame_sink_metadata.rir_support.reset();
  frame_sink_metadata.rir_support =
      MakeRenderInputRouterSupport(rir, child_frame_sink_id);
  frame_sink_metadata.rir_support->NotifySiteIsMobileOptimized(
      frame_sink_metadata.is_mobile_optimized);
}

std::unique_ptr<RenderInputRouterSupportBase>
InputManager::MakeRenderInputRouterSupport(input::RenderInputRouter* rir,
                                           const FrameSinkId& frame_sink_id) {
  TRACE_EVENT_INSTANT("input", "InputManager::MakeRenderInputRouterSupport");
  auto parent_id =
      frame_sink_manager_->GetOldestParentByChildFrameId(frame_sink_id);
  if (frame_sink_manager_->IsFrameSinkIdInRootSinkMap(parent_id)) {
    // InputVizard only supports Android currently.
    NOTREACHED();
  }
  return std::make_unique<RenderInputRouterSupportChildFrame>(rir, this,
                                                              frame_sink_id);
}

void InputManager::OnRIRDelegateClientDisconnected(
    const FrameSinkId& frame_sink_id) {
  rir_delegate_remote_map_.erase(frame_sink_id);
}


}  // namespace viz
