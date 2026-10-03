// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/system_menu_model_builder.h"

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/glic/public/glic_enabling.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/tabs/features.h"
#include "chrome/browser/ui/tabs/vertical_tab_strip_state_controller.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/frame/immersive_mode_controller.h"
#include "chrome/browser/ui/web_applications/app_browser_controller.h"
#include "chrome/browser/user_education/user_education_service.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/grit/generated_resources.h"
#include "components/strings/grit/components_strings.h"
#include "components/user_education/common/new_badge/new_badge_controller.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/models/menu_model.h"
#include "ui/menus/simple_menu_model.h"


#if BUILDFLAG(IS_OZONE) && !BUILDFLAG(IS_CHROMEOS)
#include "ui/ozone/public/ozone_platform.h"
#endif

DEFINE_CLASS_ELEMENT_IDENTIFIER_VALUE(SystemMenuModelBuilder,
                                      kToggleVerticalTabsElementId);

SystemMenuModelBuilder::SystemMenuModelBuilder(
    ui::AcceleratorProvider* provider,
    Browser* browser)
    : menu_delegate_(provider, browser) {}

SystemMenuModelBuilder::~SystemMenuModelBuilder() = default;

void SystemMenuModelBuilder::Init() {
  ui::SimpleMenuModel* model = new ui::SimpleMenuModel(&menu_delegate_);
  menu_model_.reset(model);
  BuildMenu(model);
}

void SystemMenuModelBuilder::BuildMenu(ui::SimpleMenuModel* model) {
  // We add the menu items in reverse order so that insertion_index never needs
  // to change.
  if (browser()->is_type_normal()) {
    BuildSystemMenuForBrowserWindow(model);
  } else {
    BuildSystemMenuForAppOrPopupWindow(model);
  }
}

void SystemMenuModelBuilder::BuildSystemMenuForBrowserWindow(
    ui::SimpleMenuModel* model) {
#if BUILDFLAG(IS_LINUX)
  model->AddItemWithStringId(IDC_MINIMIZE_WINDOW, IDS_MINIMIZE_WINDOW_MENU);
  model->AddItemWithStringId(IDC_MAXIMIZE_WINDOW, IDS_MAXIMIZE_WINDOW_MENU);
  model->AddItemWithStringId(IDC_RESTORE_WINDOW, IDS_RESTORE_WINDOW_MENU);
  model->AddSeparator(ui::NORMAL_SEPARATOR);
#endif
  model->AddItemWithStringId(IDC_NEW_TAB, IDS_NEW_TAB);
  model->AddItemWithStringId(IDC_RESTORE_TAB, IDS_RESTORE_TAB);

  if (features::IsTabGroupMenuMoreEntryPointsEnabled()) {
    model->AddItemWithStringId(IDC_GROUP_UNGROUPED_TABS,
                               IDS_GROUP_UNGROUPED_TABS);
  }

  model->AddItemWithStringId(IDC_BOOKMARK_ALL_TABS, IDS_BOOKMARK_ALL_TABS);
  model->AddItemWithStringId(IDC_NAME_WINDOW, IDS_NAME_WINDOW);
    model->AddSeparator(ui::NORMAL_SEPARATOR);
    model->AddItemWithStringId(IDC_GLIC_TOGGLE_PIN, IDS_GLIC_PIN);

  if (auto* controller =
          tabs::VerticalTabStripStateController::From(browser())) {
    // TODO(crbug.com/475222200): When in immersive, swapping between tab
    // strip types create duplicate tab strips. Until that is resolved, disable
    // the ability to swap between tab strips while in immersive.
    if (!ImmersiveModeController::From(browser())->IsEnabled()) {
      model->AddSeparator(ui::NORMAL_SEPARATOR);
      if (controller->ShouldDisplayVerticalTabs()) {
        model->AddItemWithStringId(IDC_TOGGLE_VERTICAL_TABS,
                                   IDS_SWITCH_TO_HORIZONTAL_TAB);
      } else {
        model->AddItemWithStringId(IDC_TOGGLE_VERTICAL_TABS,
                                   IDS_SWITCH_TO_VERTICAL_TAB);
        const bool use_preview_badge =
            base::FeatureList::IsEnabled(tabs::kVerticalTabsPreviewBadge);
        const ui::NewBadgeType badge_type = use_preview_badge
                                                ? ui::NewBadgeType::kPreview
                                                : ui::NewBadgeType::kNew;
        const user_education::DisplayNewBadge show_badge =
            UserEducationService::MaybeShowNewBadge(
                browser()->GetProfile(), use_preview_badge
                                             ? tabs::kVerticalTabsPreviewBadge
                                             : tabs::kVerticalTabsNewBadge);
        model->SetIsNewFeatureAt(
            model->GetIndexOfCommandId(IDC_TOGGLE_VERTICAL_TABS).value(),
            show_badge, badge_type);
      }
      model->SetElementIdentifierAt(
          model->GetIndexOfCommandId(IDC_TOGGLE_VERTICAL_TABS).value(),
          kToggleVerticalTabsElementId);
      model->AddItemWithStringId(IDC_VERTICAL_TABS_SEND_FEEDBACK,
                                 IDS_VERTICAL_TABS_SEND_FEEDBACK);
    }
  }

  if (chrome::CanOpenTaskManager()) {
    model->AddSeparator(ui::NORMAL_SEPARATOR);
    model->AddItemWithStringId(IDC_TASK_MANAGER_CONTEXT_MENU, IDS_TASK_MANAGER);
  }
#if BUILDFLAG(IS_LINUX)
  model->AddSeparator(ui::NORMAL_SEPARATOR);
  bool supports_server_side_decorations = true;
#if BUILDFLAG(IS_OZONE) && !BUILDFLAG(IS_CHROMEOS)
  supports_server_side_decorations =
      ui::OzonePlatform::GetInstance()
          ->GetPlatformRuntimeProperties()
          .supports_server_side_window_decorations;
#endif
  if (supports_server_side_decorations) {
    model->AddCheckItemWithStringId(IDC_USE_SYSTEM_TITLE_BAR,
                                    IDS_SHOW_WINDOW_DECORATIONS_MENU);
  }
  model->AddSeparator(ui::NORMAL_SEPARATOR);
  model->AddItemWithStringId(IDC_CLOSE_WINDOW, IDS_CLOSE_WINDOW_MENU);
#endif
  AppendTeleportMenu(model);
  // If it's a regular browser window with tabs, we don't add any more items,
  // since it already has menus (Page, Chrome).
}

void SystemMenuModelBuilder::BuildSystemMenuForAppOrPopupWindow(
    ui::SimpleMenuModel* model) {
  model->AddItemWithStringId(IDC_BACK, IDS_CONTENT_CONTEXT_BACK);
  model->AddItemWithStringId(IDC_FORWARD, IDS_CONTENT_CONTEXT_FORWARD);
  model->AddItemWithStringId(IDC_RELOAD, IDS_APP_MENU_RELOAD);
  if (!web_app::AppBrowserController::IsWebApp(browser())) {
    bool is_captive_portal_signin = false;
    if (!is_captive_portal_signin) {
      model->AddSeparator(ui::NORMAL_SEPARATOR);
      if (browser()->is_type_app() || browser()->is_type_app_popup()) {
        model->AddItemWithStringId(IDC_NEW_TAB, IDS_APP_MENU_NEW_WEB_PAGE);
      } else {
        model->AddItemWithStringId(IDC_SHOW_AS_TAB, IDS_SHOW_AS_TAB);
      }
    }
    model->AddSeparator(ui::NORMAL_SEPARATOR);
    model->AddItemWithStringId(IDC_CUT, IDS_CUT);
    model->AddItemWithStringId(IDC_COPY, IDS_COPY);
    model->AddItemWithStringId(IDC_PASTE, IDS_PASTE);
    model->AddSeparator(ui::NORMAL_SEPARATOR);
    model->AddItemWithStringId(IDC_FIND, IDS_FIND);
    model->AddItemWithStringId(IDC_PRINT, IDS_PRINT);
    zoom_menu_contents_ =
        std::make_unique<ui::SimpleMenuModel>(&menu_delegate_);
    zoom_menu_contents_->AddItemWithStringId(IDC_ZOOM_PLUS, IDS_ZOOM_PLUS);
    zoom_menu_contents_->AddItemWithStringId(IDC_ZOOM_NORMAL, IDS_ZOOM_NORMAL);
    zoom_menu_contents_->AddItemWithStringId(IDC_ZOOM_MINUS, IDS_ZOOM_MINUS);
    model->AddSubMenuWithStringId(IDC_ZOOM_MENU, IDS_ZOOM_MENU,
                                  zoom_menu_contents_.get());
  }

  bool should_show_task_manager =
      (browser()->is_type_app() || browser()->is_type_app_popup()) &&
      chrome::CanOpenTaskManager();
  if (should_show_task_manager) {
    model->AddSeparator(ui::NORMAL_SEPARATOR);
    model->AddItemWithStringId(IDC_TASK_MANAGER, IDS_TASK_MANAGER);
  }
#if BUILDFLAG(IS_LINUX)
  model->AddSeparator(ui::NORMAL_SEPARATOR);
  model->AddItemWithStringId(IDC_CLOSE_WINDOW, IDS_CLOSE);
#endif
  AppendTeleportMenu(model);
}


void SystemMenuModelBuilder::AppendTeleportMenu(ui::SimpleMenuModel* model) {
}
