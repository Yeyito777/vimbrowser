// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_APPS_APP_INFO_DIALOG_APP_INFO_FOOTER_PANEL_H_
#define CHROME_BROWSER_UI_VIEWS_APPS_APP_INFO_DIALOG_APP_INFO_FOOTER_PANEL_H_

#include <memory>

#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/extensions/extension_uninstall_dialog.h"
#include "chrome/browser/ui/views/apps/app_info_dialog/app_info_panel.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/metadata/view_factory.h"

class Profile;

namespace extensions {
class Extension;
}

// A small summary panel with buttons to control the app that is displayed at
// the bottom of the app info dialog.
class AppInfoFooterPanel
    : public AppInfoPanel,
      public extensions::ExtensionUninstallDialog::Delegate {
  METADATA_HEADER(AppInfoFooterPanel, AppInfoPanel)

 public:
  AppInfoFooterPanel(Profile* profile, const extensions::Extension* app);
  AppInfoFooterPanel(const AppInfoFooterPanel&) = delete;
  AppInfoFooterPanel& operator=(const AppInfoFooterPanel&) = delete;
  ~AppInfoFooterPanel() override;

  // This can return null if the footer panel contains no shortcuts, cannot be
  // pinned to the shelf and the app cannot be uninstalled.
  static std::unique_ptr<AppInfoFooterPanel> CreateFooterPanel(
      Profile* profile,
      const extensions::Extension* app);

 private:
  FRIEND_TEST_ALL_PREFIXES(AppInfoDialogViewsTest,
                           PinButtonsAreFocusedAfterPinUnpin);

  void CreateButtons();


  // Overridden from ExtensionUninstallDialog::Delegate:
  void OnExtensionUninstallDialogClosed(bool did_start_uninstall,
                                        const std::u16string& error) override;

  // Create Shortcuts for the app.
  void CreateShortcuts();
  static bool CanCreateShortcuts(const extensions::Extension* app);


  // Uninstall the app. Must only be called if CanUninstallApp() returns true.
  void UninstallApp();
  static bool CanUninstallApp(Profile* profile,
                              const extensions::Extension* app);

  // UI elements on the dialog. Elements are null if they are not displayed.
  raw_ptr<views::View> create_shortcuts_button_ = nullptr;
  raw_ptr<views::View> remove_button_ = nullptr;

  std::unique_ptr<extensions::ExtensionUninstallDialog>
      extension_uninstall_dialog_;
};

BEGIN_VIEW_BUILDER(/* no export */, AppInfoFooterPanel, AppInfoPanel)
END_VIEW_BUILDER

DEFINE_VIEW_BUILDER(/* no export */, AppInfoFooterPanel)

#endif  // CHROME_BROWSER_UI_VIEWS_APPS_APP_INFO_DIALOG_APP_INFO_FOOTER_PANEL_H_
