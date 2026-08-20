// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SECURITY_INTERSTITIALS_CORE_INSECURE_FORM_UTIL_H_
#define COMPONENTS_SECURITY_INTERSTITIALS_CORE_INSECURE_FORM_UTIL_H_

#include "build/build_config.h"

class GURL;

namespace url {
class Origin;
}

namespace security_interstitials {


// Returns true if submitting a form with the given source and action urls is
// insecure.
// `source_origin` is the Origin of the page that submits the form.
// `action_url` is the URL of the form's action attribute.
bool IsInsecureFormActionOnSecureSource(const url::Origin& source_origin,
                                        const GURL& action_url);

// Returns true if submitting a form with the given action url is insecure.
// Matches the blink check for mixed content at
// blink::MixedContentChecker::IsMixedFormAction().
// `action_url` is the URL of the form's action attribute.
bool IsInsecureFormAction(const GURL& action_url);

}  // namespace security_interstitials

#endif  // COMPONENTS_SECURITY_INTERSTITIALS_CORE_INSECURE_FORM_UTIL_H_
