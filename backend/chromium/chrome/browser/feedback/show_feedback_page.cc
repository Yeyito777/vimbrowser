// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/feedback/show_feedback_page.h"

#include <string>

#include "base/json/json_writer.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/escape.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "chrome/browser/feedback/feedback_dialog_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/webui/feedback/feedback_dialog.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/base/consent_level.h"
#include "extensions/browser/api/feedback_private/feedback_private_api.h"
#include "third_party/re2/src/re2/re2.h"


namespace feedback_private = extensions::api::feedback_private;

namespace chrome {

namespace {


feedback_private::FeedbackFlow GetFeedbackFlowFromSource(
    feedback::FeedbackSource source) {
  switch (source) {
    case feedback::kFeedbackSourceSadTabPage:
      return feedback_private::FeedbackFlow::kSadTabCrash;
    case feedback::kFeedbackSourceAutofillContextMenu:
      return feedback_private::FeedbackFlow::kGoogleInternal;
    case feedback::kFeedbackSourceAI:
      return feedback_private::FeedbackFlow::kAi;
    default:
      return feedback_private::FeedbackFlow::kRegular;
  }
}

// Calls feedback private api to show Feedback ui.
void RequestFeedbackFlow(const GURL& page_url,
                         Profile* profile,
                         feedback::FeedbackSource source,
                         const std::string& description_template,
                         const std::string& description_placeholder_text,
                         const std::string& category_tag,
                         const std::string& extra_diagnostics,
                         base::DictValue autofill_metadata,
                         base::DictValue ai_metadata) {
  feedback_private::FeedbackFlow flow = GetFeedbackFlowFromSource(source);
  bool include_bluetooth_logs = false;
  bool show_questionnaire = false;

  extensions::FeedbackPrivateAPI* api =
      extensions::FeedbackPrivateAPI::GetFactoryInstance()->Get(profile);
  auto info = api->CreateFeedbackInfo(
      description_template, description_placeholder_text, category_tag,
      extra_diagnostics, page_url, flow,
      source == feedback::kFeedbackSourceAssistant, include_bluetooth_logs,
      show_questionnaire, source == feedback::kFeedbackSourceChromeLabs,
      source == feedback::kFeedbackSourceAutofillContextMenu, autofill_metadata,
      ai_metadata);

  FeedbackDialog::CreateOrShow(profile, *info);
}

}  // namespace

void ShowFeedbackPage(BrowserWindowInterface* bwi,
                      feedback::FeedbackSource source,
                      const std::string& description_template,
                      const std::string& description_placeholder_text,
                      const std::string& category_tag,
                      const std::string& extra_diagnostics,
                      base::DictValue autofill_metadata,
                      base::DictValue ai_metadata) {
  GURL page_url;
  if (bwi) {
    page_url = GetTargetTabUrl(bwi, bwi->GetTabStripModel()->active_index());
  }

  Profile* profile = GetFeedbackProfile(bwi);

  ShowFeedbackPage(page_url, profile, source, description_template,
                   description_placeholder_text, category_tag,
                   extra_diagnostics, std::move(autofill_metadata),
                   std::move(ai_metadata));
}

void ShowFeedbackPage(const GURL& page_url,
                      Profile* profile,
                      feedback::FeedbackSource source,
                      const std::string& description_template,
                      const std::string& description_placeholder_text,
                      const std::string& category_tag,
                      const std::string& extra_diagnostics,
                      base::DictValue autofill_metadata,
                      base::DictValue ai_metadata) {
  if (!profile) {
    LOG(ERROR) << "Cannot invoke feedback: No profile found!";
    return;
  }
  if (!profile->GetPrefs()->GetBoolean(prefs::kUserFeedbackAllowed)) {
    return;
  }
  // Record an UMA histogram to know the most frequent feedback request source.
  UMA_HISTOGRAM_ENUMERATION("Feedback.RequestSource", source,
                            feedback::kFeedbackSourceCount);

  // Show feedback dialog using feedback extension API.
  RequestFeedbackFlow(page_url, profile, source, description_template,
                      description_placeholder_text, category_tag,
                      extra_diagnostics, std::move(autofill_metadata),
                      std::move(ai_metadata));
}

}  // namespace chrome
