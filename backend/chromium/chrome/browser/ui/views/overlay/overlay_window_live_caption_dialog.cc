// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/overlay/overlay_window_live_caption_dialog.h"

#include "base/metrics/histogram_functions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "chrome/grit/generated_resources.h"
#include "components/live_caption/pref_names.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace {

constexpr gfx::Size kLiveCaptionDialogSize(260, 62);

constexpr int kLiveCaptionDialogCornerRadius = 12;

constexpr int kHorizontalMarginDip = 20;
constexpr int kImageWidthDip = 20;
constexpr int kVerticalMarginDip = 10;

}  // namespace

OverlayWindowLiveCaptionDialog::OverlayWindowLiveCaptionDialog(Profile* profile)
    : profile_(profile) {
  SetSize(kLiveCaptionDialogSize);
  SetBackground(views::CreateRoundedRectBackground(
      ui::kColorSysSurface, kLiveCaptionDialogCornerRadius));
  SetLayoutManager(std::make_unique<views::BoxLayout>(
                       views::BoxLayout::Orientation::kVertical))
      ->set_cross_axis_alignment(views::BoxLayout::CrossAxisAlignment::kCenter);

  auto live_caption_container = std::make_unique<View>();

  auto live_caption_image = std::make_unique<views::ImageView>();
  live_caption_image->SetImage(ui::ImageModel::FromVectorIcon(
      vector_icons::kLiveCaptionOnIcon, ui::kColorIcon, kImageWidthDip));
  live_caption_container->AddChildView(std::move(live_caption_image));

  auto live_caption_title =
      std::make_unique<views::Label>(l10n_util::GetStringUTF16(
          IDS_PICTURE_IN_PICTURE_LIVE_CAPTION_CONTROL_TEXT));
  live_caption_title->SetHorizontalAlignment(
      gfx::HorizontalAlignment::ALIGN_LEFT);
  live_caption_title->SetMultiLine(true);
  live_caption_title_ =
      live_caption_container->AddChildView(std::move(live_caption_title));

  auto live_caption_button =
      std::make_unique<views::ToggleButton>(base::BindRepeating(
          &OverlayWindowLiveCaptionDialog::OnLiveCaptionButtonPressed,
          base::Unretained(this)));
  live_caption_button->SetIsOn(
      profile_->GetPrefs()->GetBoolean(prefs::kLiveCaptionEnabled));
  live_caption_button->GetViewAccessibility().SetName(
      std::u16string(live_caption_title_->GetText()));
  live_caption_button_ =
      live_caption_container->AddChildView(std::move(live_caption_button));

  auto* live_caption_container_layout =
      live_caption_container->SetLayoutManager(
          std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kHorizontal,
              gfx::Insets::VH(kVerticalMarginDip, kHorizontalMarginDip),
              ChromeLayoutProvider::Get()->GetDistanceMetric(
                  DISTANCE_RICH_HOVER_BUTTON_ICON_HORIZONTAL)));
  live_caption_container_layout->SetFlexForView(live_caption_title_, 1);
  AddChildView(std::move(live_caption_container));

  pref_change_registrar_ = std::make_unique<PrefChangeRegistrar>();
  pref_change_registrar_->Init(profile->GetPrefs());
  pref_change_registrar_->Add(
      prefs::kLiveCaptionEnabled,
      base::BindRepeating(
          &OverlayWindowLiveCaptionDialog::OnLiveCaptionEnabledChanged,
          base::Unretained(this)));
}

OverlayWindowLiveCaptionDialog::~OverlayWindowLiveCaptionDialog() = default;

void OverlayWindowLiveCaptionDialog::OnLiveCaptionButtonPressed() {
  bool enabled = !profile_->GetPrefs()->GetBoolean(prefs::kLiveCaptionEnabled);
  profile_->GetPrefs()->SetBoolean(prefs::kLiveCaptionEnabled, enabled);
  base::UmaHistogramBoolean(
      "Accessibility.LiveCaption.EnableFromVideoPictureInPicture", enabled);
}

void OverlayWindowLiveCaptionDialog::OnLiveCaptionEnabledChanged() {
  bool enabled = profile_->GetPrefs()->GetBoolean(prefs::kLiveCaptionEnabled);
  live_caption_button_->SetIsOn(enabled);
}

BEGIN_METADATA(OverlayWindowLiveCaptionDialog)
END_METADATA
