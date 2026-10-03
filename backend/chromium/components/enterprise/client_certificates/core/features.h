// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ENTERPRISE_CLIENT_CERTIFICATES_CORE_FEATURES_H_
#define COMPONENTS_ENTERPRISE_CLIENT_CERTIFICATES_CORE_FEATURES_H_

#include "base/feature_list.h"
#include "build/build_config.h"

namespace client_certificates::features {

// Controls whether user client certs storage relies on prefs or LevelDB.
BASE_DECLARE_FEATURE(kManagedUserClientCertificateInPrefs);

// Return true if the managed user certificate should be stored in prefs.
bool IsManagedUserClientCertificateInPrefsEnabled();

// Controls whether client certificate provisioning on Android is enabled.
BASE_DECLARE_FEATURE(kEnableClientCertificateProvisioningOnAndroid);

// Return true if client certificate provisioning on Android is enabled.
bool IsClientCertificateProvisioningOnAndroidEnabled();

// Controls whether client certificate provisioning on iOS is enabled.
BASE_DECLARE_FEATURE(kEnableClientCertificateProvisioningOnIOS);

// Return true if client certificate provisioning on iOS is enabled.
bool IsClientCertificateProvisioningOnIOSEnabled();


}  // namespace client_certificates::features

#endif  // COMPONENTS_ENTERPRISE_CLIENT_CERTIFICATES_CORE_FEATURES_H_
