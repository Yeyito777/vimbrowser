// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/feedback/feedback_dialog_utils.h"

#include "build/build_config.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/feedback/show_feedback_page.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"


namespace chrome {

GURL GetTargetTabUrl(BrowserWindowInterface* browser, int index) {
  // Sanity checks.
  if (!browser || index >= browser->GetTabStripModel()->count()) {
    return GURL();
  }

  if (index >= 0) {
    content::WebContents* target_tab =
        browser->GetTabStripModel()->GetWebContentsAt(index);
    if (target_tab) {
      if (browser->GetType() == BrowserWindowInterface::TYPE_DEVTOOLS) {
        if (auto* dev_tools_window =
                DevToolsWindow::AsDevToolsWindow(target_tab)) {
          target_tab = dev_tools_window->GetInspectedWebContents();
        }
      }
      if (target_tab)
        return target_tab->GetLastCommittedURL();
    }
  }

  return GURL();
}

Profile* GetFeedbackProfile(BrowserWindowInterface* bwi) {
  Profile* profile = bwi ? bwi->GetProfile()
                         : ProfileManager::GetLastUsedProfileAllowedByPolicy();
  if (!profile)
    return nullptr;

  // We do not want to launch on an OTR profile.
  profile = profile->GetOriginalProfile();
  DCHECK(profile);

  return profile;
}

void ShowFeedbackDialogForWebUI(WebUIFeedbackSource webui_source,
                                const std::string& extra_diagnostics) {
  feedback::FeedbackSource source;
  std::string category;
  switch (webui_source) {
    case WebUIFeedbackSource::kConnectivityDiagnostics:
      source = feedback::FeedbackSource::kFeedbackSourceConnectivityDiagnostics;
      category = "connectivity-diagnostics";
      break;
  }

  ShowFeedbackPage(nullptr, source,
                   /*description_template=*/std::string(),
                   /*description_placeholder_text=*/std::string(), category,
                   extra_diagnostics);
}

}  // namespace chrome
