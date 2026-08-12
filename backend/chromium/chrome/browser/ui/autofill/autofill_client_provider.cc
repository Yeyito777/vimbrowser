// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/autofill/autofill_client_provider.h"

#include "base/check_deref.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/password_manager/chrome_password_manager_client.h"
#include "chrome/browser/ui/autofill/chrome_autofill_client.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "content/public/browser/web_contents.h"


namespace autofill {
namespace {


bool UsesVirtualViewStructureForAutofill(PrefService& prefs) {
  return false;
}

}  // namespace

AutofillClientProvider::AutofillClientProvider(PrefService* prefs)
    : uses_platform_autofill_(
          UsesVirtualViewStructureForAutofill(CHECK_DEREF(prefs))) {
}

AutofillClientProvider::~AutofillClientProvider() = default;

void AutofillClientProvider::CreateClientForWebContents(
    content::WebContents* web_contents) {
  if (uses_platform_autofill()) {
    NOTREACHED();
  } else {
    ChromeAutofillClient::CreateForWebContents(web_contents);
  }
}

}  // namespace autofill
