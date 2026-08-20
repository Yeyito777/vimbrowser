// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/apps/app_info_dialog/app_info_footer_panel.h"

#include "base/functional/callback_helpers.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/dialogs/browser_dialogs.h"
#include "chrome/browser/ui/views/chrome_layout_provider.h"
#include "chrome/grit/generated_resources.h"
#include "components/app_constants/constants.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/management_policy.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/extension.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"


AppInfoFooterPanel::AppInfoFooterPanel(Profile* profile,
                                       const extensions::Extension* app)
    : AppInfoPanel(profile, app) {
  ChromeLayoutProvider* provider = ChromeLayoutProvider::Get();

  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      provider->GetInsetsMetric(views::INSETS_DIALOG_SUBSECTION),
      provider->GetDistanceMetric(views::DISTANCE_RELATED_BUTTON_HORIZONTAL)));

  CreateButtons();
}

AppInfoFooterPanel::~AppInfoFooterPanel() = default;

// static
std::unique_ptr<AppInfoFooterPanel> AppInfoFooterPanel::CreateFooterPanel(
    Profile* profile,
    const extensions::Extension* app) {
  if (CanCreateShortcuts(app) ||
      CanUninstallApp(profile, app))
    return std::make_unique<AppInfoFooterPanel>(profile, app);
  return nullptr;
}

void AppInfoFooterPanel::CreateButtons() {
  if (CanCreateShortcuts(app_)) {
    create_shortcuts_button_ =
        AddChildView(std::make_unique<views::MdTextButton>(
            base::BindRepeating(&AppInfoFooterPanel::CreateShortcuts,
                                base::Unretained(this)),
            l10n_util::GetStringUTF16(
                IDS_APPLICATION_INFO_CREATE_SHORTCUTS_BUTTON_TEXT)));
  }


  if (CanUninstallApp(profile_, app_)) {
    remove_button_ = AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&AppInfoFooterPanel::UninstallApp,
                            base::Unretained(this)),
        l10n_util::GetStringUTF16(IDS_APPLICATION_INFO_UNINSTALL_BUTTON_TEXT)));
  }
}


void AppInfoFooterPanel::OnExtensionUninstallDialogClosed(
    bool did_start_uninstall,
    const std::u16string& error) {
  if (did_start_uninstall) {
    // Close the App Info dialog as well (which will free the dialog too).
    Close();
  } else {
    extension_uninstall_dialog_.reset();
  }
}

void AppInfoFooterPanel::CreateShortcuts() {
  DCHECK(CanCreateShortcuts(app_));
  chrome::ShowCreateChromeAppShortcutsDialog(GetWidget()->GetNativeWindow(),
                                             profile_, app_, base::DoNothing());
}

// static
bool AppInfoFooterPanel::CanCreateShortcuts(const extensions::Extension* app) {
  // Extensions and the Chrome component app can't have shortcuts.
  return app->id() != app_constants::kChromeAppId && !app->is_extension();
}


void AppInfoFooterPanel::UninstallApp() {
  DCHECK(CanUninstallApp(profile_, app_));
  extension_uninstall_dialog_ = extensions::ExtensionUninstallDialog::Create(
      profile_, GetWidget()->GetNativeWindow(), this);
  extension_uninstall_dialog_->ConfirmUninstall(
      app_.get(), extensions::UNINSTALL_REASON_USER_INITIATED,
      extensions::UNINSTALL_SOURCE_APP_INFO_DIALOG);
}

// static
bool AppInfoFooterPanel::CanUninstallApp(Profile* profile,
                                         const extensions::Extension* app) {
  extensions::ManagementPolicy* policy =
      extensions::ExtensionSystem::Get(profile)->management_policy();
  return policy->UserMayModifySettings(app, nullptr) &&
         !policy->MustRemainInstalled(app, nullptr);
}

BEGIN_METADATA(AppInfoFooterPanel)
END_METADATA
