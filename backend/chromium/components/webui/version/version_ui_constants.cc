// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/webui/version/version_ui_constants.h"

#include "build/build_config.h"
#include "cef/libcef/features/features.h"

namespace version_ui {

// Message handlers.
const char kRequestVersionInfo[] = "requestVersionInfo";
const char kRequestVariationInfo[] = "requestVariationInfo";
const char kRequestPathInfo[] = "requestPathInfo";

// Named keys used in message handler responses.
const char kKeyVariationsList[] = "variationsList";
const char kKeyVariationsCmd[] = "variationsCmd";
const char kKeyExecPath[] = "execPath";
const char kKeyProfilePath[] = "profilePath";

// Strings.
const char kApplicationLabel[] = "application_label";
const char kCL[] = "cl";
const char kCommandLine[] = "command_line";
const char kCommandLineName[] = "command_line_name";
const char kCompany[] = "company";
const char kCopyright[] = "copyright";
const char kExecutablePath[] = "executable_path";
const char kExecutablePathName[] = "executable_path_name";
const char kJSEngine[] = "js_engine";
const char kJSVersion[] = "js_version";
const char kLogoAltText[] = "logo_alt_text";
const char kOfficial[] = "official";
const char kOSName[] = "os_name";
const char kOSType[] = "os_type";
const char kProfilePath[] = "profile_path";
const char kProfilePathName[] = "profile_path_name";
const char kCopyLabel[] = "copy_label";
const char kCopyNotice[] = "copy_notice";
const char kRevision[] = "revision";
const char kSanitizer[] = "sanitizer";
const char kTitle[] = "title";
const char kUserAgent[] = "useragent";
const char kUserAgentName[] = "user_agent_name";
const char kVariationsCmdName[] = "variations_cmd_name";
const char kCopyVariationsLabel[] = "copy_variations_label";
const char kCopyVariationsNotice[] = "copy_variations_notice";
const char kVariationsName[] = "variations_name";
const char kVariationsSeed[] = "variations_seed";
const char kVariationsSeedName[] = "variations_seed_name";
const char kVersion[] = "version";
const char kVersionSuffix[] = "version_suffix";
const char kVersionModifier[] = "version_modifier";
const char kVersionProcessorVariation[] = "version_processor_variation";

#if BUILDFLAG(ENABLE_CEF)
const char kKeyModulePath[] = "modulePath";
const char kKeyUserDataPath[] = "userDataPath";

const char kCefVersion[] = "cef_version";
const char kModulePath[] = "module_path";
const char kModulePathName[] = "module_path_name";
const char kUserDataPath[] = "user_data_path";
const char kUserDataPathName[] = "user_data_path_name";
#endif

}  // namespace version_ui
