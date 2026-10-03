// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/service/display/overlay_processor_ozone.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/timer/elapsed_timer.h"
#include "build/build_config.h"
#include "build/chromecast_buildflags.h"
#include "components/viz/common/buildflags.h"
#include "components/viz/common/features.h"
#include "components/viz/service/display/overlay_strategy_fullscreen.h"
#include "components/viz/service/display/overlay_strategy_single_on_top.h"
#include "components/viz/service/display/overlay_strategy_underlay.h"
#include "gpu/command_buffer/service/shared_image/shared_image_format_service_utils.h"
#include "ui/base/ui_base_features.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/size_conversions.h"


#if BUILDFLAG(ENABLE_CAST_OVERLAY_STRATEGY)
#include "components/viz/service/display/overlay_strategy_underlay_cast.h"
#endif

namespace viz {

namespace {

gfx::ColorSpace GetColorSpaceForOzone(SharedImageFormat format,
                                      const gfx::ColorSpace& orig_color_space) {
  return orig_color_space;
}

void ConvertToOzoneOverlaySurface(
    const OverlayCandidate& overlay_candidate,
    ui::OverlaySurfaceCandidate* ozone_candidate) {
  ozone_candidate->transform = overlay_candidate.transform;
  ozone_candidate->format = overlay_candidate.format;
  ozone_candidate->color_space =
      GetColorSpaceForOzone(ozone_candidate->format,
                            /*orig_color_space=*/overlay_candidate.color_space);
  ozone_candidate->display_rect = overlay_candidate.display_rect;
  ozone_candidate->crop_rect = overlay_candidate.uv_rect;
  ozone_candidate->clip_rect = overlay_candidate.clip_rect;
  ozone_candidate->is_opaque = overlay_candidate.is_opaque;
  ozone_candidate->opacity = overlay_candidate.opacity;
  ozone_candidate->plane_z_order = overlay_candidate.plane_z_order;
  ozone_candidate->buffer_size = overlay_candidate.resource_size_in_pixels;
  ozone_candidate->requires_overlay = overlay_candidate.requires_overlay;
  ozone_candidate->priority_hint = overlay_candidate.priority_hint;
  ozone_candidate->rounded_corners = overlay_candidate.rounded_corners;
  ozone_candidate->overlay_type = overlay_candidate.overlay_type;
  // TODO(crbug.com/40219248): OverlaySurfaceCandidate to SkColor4f
  // That can be a solid color quad.
  if (!overlay_candidate.is_solid_color && ozone_candidate->background_color &&
      overlay_candidate.color) {
    ozone_candidate->background_color = overlay_candidate.color->toSkColor();
  }
}

void ConvertToTiledOzoneOverlaySurface(
    const OverlayCandidate& overlay_candidate,
    ui::OverlaySurfaceCandidate* ozone_candidate) {
  ozone_candidate->transform = gfx::OVERLAY_TRANSFORM_NONE;
  ozone_candidate->format = SinglePlaneFormat::kRGBA_8888;
  ozone_candidate->color_space =
      GetColorSpaceForOzone(ozone_candidate->format,
                            /*orig_color_space=*/overlay_candidate.color_space);
  ozone_candidate->display_rect = overlay_candidate.display_rect;
  ozone_candidate->crop_rect = gfx::RectF(1.0, 1.0);
  ozone_candidate->clip_rect = std::nullopt;
  ozone_candidate->is_opaque = overlay_candidate.is_opaque;
  ozone_candidate->opacity = overlay_candidate.opacity;
  ozone_candidate->plane_z_order = overlay_candidate.plane_z_order;
  ozone_candidate->buffer_size =
      gfx::Size(static_cast<int>(overlay_candidate.display_rect.width()),
                static_cast<int>(overlay_candidate.display_rect.height()));
  ozone_candidate->requires_overlay = true;
  ozone_candidate->priority_hint = overlay_candidate.priority_hint;
  ozone_candidate->rounded_corners = overlay_candidate.rounded_corners;
  ozone_candidate->native_pixmap = nullptr;
  ozone_candidate->overlay_type = overlay_candidate.overlay_type;
}


}  // namespace

// |overlay_candidates| is an object used to answer questions about possible
// overlays configurations.
// |available_strategies| is a list of overlay strategies that should be
// initialized by InitializeStrategies.
OverlayProcessorOzone::OverlayProcessorOzone(
    std::unique_ptr<ui::OverlayCandidatesOzone> overlay_candidates,
    std::vector<OverlayStrategy> available_strategies,
    std::unique_ptr<PixmapProvider> pixmap_provider)
    : overlay_candidates_(std::move(overlay_candidates)),
      available_strategies_(std::move(available_strategies)),
      pixmap_provider_(std::move(pixmap_provider)) {
  for (OverlayStrategy strategy : available_strategies_) {
    switch (strategy) {
      case OverlayStrategy::kFullscreen:
        strategies_.push_back(
            std::make_unique<OverlayStrategyFullscreen>(this));
        break;
      case OverlayStrategy::kSingleOnTop:
        strategies_.push_back(
            std::make_unique<OverlayStrategySingleOnTop>(this));
        break;
      case OverlayStrategy::kUnderlay:
        strategies_.push_back(std::make_unique<OverlayStrategyUnderlay>(this));
        break;
#if BUILDFLAG(ENABLE_CAST_OVERLAY_STRATEGY)
      case OverlayStrategy::kUnderlayCast:
        strategies_.push_back(
            std::make_unique<OverlayStrategyUnderlayCast>(this));
        break;
#endif
      default:
        NOTREACHED();
    }
  }
}

OverlayProcessorOzone::~OverlayProcessorOzone() = default;

bool OverlayProcessorOzone::IsOverlaySupported() const {
  return true;
}

bool OverlayProcessorOzone::NeedsSurfaceDamageRectList() const {
  return true;
}

bool OverlayProcessorOzone::SupportsFlipRotateTransform() const {
  // TODO(petermcneeley): Test and enable for ChromeOS.
  return false;
}

void OverlayProcessorOzone::NotifyOverlayPromotion(
    DisplayResourceProvider* display_resource_provider,
    const OverlayCandidateList& candidate_list,
    const QuadList& quad_list) {
  if (!overlay_candidates_) {
    return;
  }

  std::vector<gfx::OverlayType> promoted_overlay_types;
  promoted_overlay_types.reserve(candidate_list.size());
  for (const auto& candidate : candidate_list) {
    promoted_overlay_types.emplace_back(candidate.overlay_type);
  }

  overlay_candidates_->NotifyOverlayPromotion(
      std::move(promoted_overlay_types));
}

void OverlayProcessorOzone::CheckOverlaySupportImpl(
    const std::optional<OverlayCandidate>& primary_plane,
    OverlayCandidateList* surfaces) {
  MaybeObserveHardwareCapabilities();

  auto full_size = surfaces->size();
  if (primary_plane)
    full_size += 1;

  ui::OverlayCandidatesOzone::OverlaySurfaceCandidateList ozone_surface_list(
      full_size);

  // Convert OverlayCandidateList to OzoneSurfaceCandidateList.
  {
    auto ozone_surface_iterator = ozone_surface_list.begin();

    // For ozone-cast, there will not be a primary_plane.
    if (primary_plane) {
      ConvertToOzoneOverlaySurface(*primary_plane, &(*ozone_surface_iterator));
      // TODO(crbug.com/40153057): Fuchsia claims support for presenting primary
      // plane as overlay, but does not provide a mailbox. Handle this case.
      if (pixmap_provider_) {
        bool result = SetNativePixmapForCandidate(&(*ozone_surface_iterator),
                                                  primary_plane->mailbox,
                                                  /*is_primary=*/true);
        // We cannot validate an overlay configuration without the buffer for
        // primary plane present.
        if (!result) {
          for (auto& candidate : *surfaces) {
            candidate.overlay_handled = false;
          }
          return;
        }
      }
      ozone_surface_iterator++;
    }

    auto surface_iterator = surfaces->cbegin();
    for (; ozone_surface_iterator < ozone_surface_list.end() &&
           surface_iterator < surfaces->cend();
         ozone_surface_iterator++, surface_iterator++) {
      if (surface_iterator->needs_detiling) {
        ConvertToTiledOzoneOverlaySurface(*surface_iterator,
                                          &(*ozone_surface_iterator));
        continue;
      }

      ConvertToOzoneOverlaySurface(*surface_iterator,
                                   &(*ozone_surface_iterator));
      if (pixmap_provider_) {
        bool result = SetNativePixmapForCandidate(&(*ozone_surface_iterator),
                                                  surface_iterator->mailbox,
                                                  /*is_primary=*/false);

        // Skip the candidate if the corresponding NativePixmap is not found.
        if (!result) {
          *ozone_surface_iterator = ui::OverlaySurfaceCandidate();
          ozone_surface_iterator->plane_z_order =
              surface_iterator->plane_z_order;
        }
      }
    }

  }
  overlay_candidates_->CheckOverlaySupport(&ozone_surface_list);

  // Copy information from OzoneSurfaceCandidatelist back to
  // OverlayCandidateList.
  {
    DCHECK_EQ(full_size, ozone_surface_list.size());
    auto ozone_surface_iterator = ozone_surface_list.cbegin();
    // The primary plane is always handled, and don't need to copy information.
    if (primary_plane)
      ozone_surface_iterator++;

    auto surface_iterator = surfaces->begin();
    for (; surface_iterator < surfaces->end() &&
           ozone_surface_iterator < ozone_surface_list.cend();
         surface_iterator++, ozone_surface_iterator++) {
      surface_iterator->overlay_handled =
          ozone_surface_iterator->overlay_handled;
      surface_iterator->display_rect = ozone_surface_iterator->display_rect;
    }
  }
}

void OverlayProcessorOzone::MaybeObserveHardwareCapabilities() {
  if (tried_observing_hardware_capabilities_) {
    return;
  }
  tried_observing_hardware_capabilities_ = true;

  if (overlay_candidates_) {
    overlay_candidates_->ObserveHardwareCapabilities(
        base::BindRepeating(&OverlayProcessorOzone::ReceiveHardwareCapabilities,
                            weak_ptr_factory_.GetWeakPtr()));
  }
}

void OverlayProcessorOzone::ReceiveHardwareCapabilities(
    ui::HardwareCapabilities hardware_capabilities) {
  if (hardware_capabilities.is_valid) {
    // Subtract 1 because one of these overlay capable planes will be needed for
    // the primary plane.
    int max_overlays_supported =
        hardware_capabilities.num_overlay_capable_planes - 1;
    max_overlays_considered_ =
        std::min(max_overlays_supported, max_overlays_config_);
    has_independent_cursor_plane_ =
        hardware_capabilities.has_independent_cursor_plane;

    UMA_HISTOGRAM_COUNTS_100(
        "Compositing.Display.OverlayProcessorOzone.MaxPlanesSupported",
        hardware_capabilities.num_overlay_capable_planes);

    DCHECK(overlay_candidates_);
    overlay_candidates_->SetSupportedSharedImageFormats(
        std::move(hardware_capabilities.supported_shared_image_formats));
  } else {
    // Default to attempting 1 overlay if we get an invalid response.
    max_overlays_considered_ = 1;
  }

  // Different hardware capabilities may mean a different result for a specific
  // combination of overlays, so clear this cache.
  ClearOverlayCombinationCache();
}

gfx::Rect OverlayProcessorOzone::GetOverlayDamageRectForOutputSurface(
    const OverlayCandidate& overlay) const {
  return ToEnclosedRect(overlay.display_rect);
}

void OverlayProcessorOzone::RegisterOverlayRequirement(bool requires_overlay) {
  // This can be null in unit tests.
  if (overlay_candidates_)
    overlay_candidates_->RegisterOverlayRequirement(requires_overlay);
}

void OverlayProcessorOzone::OnSwapBuffersComplete(gfx::SwapResult swap_result) {
  if (overlay_candidates_) {
    overlay_candidates_->OnSwapBuffersComplete(swap_result);
  }
}

void OverlayProcessorOzone::InsertPrimaryPlane(
    OverlayCandidate primary_plane,
    OverlayCandidateList& candidates) {
  // Ozone DRM needs the primary plane as the first overlay when overlay
  // testing.
  const auto insert_positon = candidates.begin();
  candidates.insert(insert_positon, std::move(primary_plane));
}

bool OverlayProcessorOzone::ShouldCreatePrimaryPlane() const {
  return true;
}

bool OverlayProcessorOzone::SetNativePixmapForCandidate(
    ui::OverlaySurfaceCandidate* candidate,
    const gpu::Mailbox& mailbox,
    bool is_primary) {
  DCHECK(pixmap_provider_);
  scoped_refptr<gfx::NativePixmap> native_pixmap =
      pixmap_provider_->GetNativePixmap(mailbox);

  if (!native_pixmap) {
    // SharedImage creation and destruction happens on a different
    // thread so there is no guarantee that we can always look them up
    // successfully. If a SharedImage doesn't exist, ignore the
    // candidate. We will try again next frame.
    DLOG(ERROR) << "Unable to find the NativePixmap corresponding to the "
                   "overlay candidate";
    return false;
  }

  if (is_primary &&
      (candidate->buffer_size != native_pixmap->GetBufferSize() ||
       candidate->format != native_pixmap->GetSharedImageFormat())) {
    // If |mailbox| corresponds to the last submitted primary plane, its
    // parameters may not match those of the current candidate due to a
    // reshape. If the size and format don't match, skip this candidate for
    // now, and try again next frame.
    return false;
  }

  candidate->native_pixmap = std::move(native_pixmap);
  return true;
}

OverlayProcessorOzone::PixmapProvider::~PixmapProvider() = default;

}  // namespace viz
