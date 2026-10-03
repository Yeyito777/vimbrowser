// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_WEBUI_VERSION_VERSION_UI_CONSTANTS_H_
#define COMPONENTS_WEBUI_VERSION_VERSION_UI_CONSTANTS_H_

#include "build/build_config.h"
#include "cef/libcef/features/features.h"

namespace version_ui {

// Message handlers.
// Must match the constants used in the resource files.
extern const char kRequestVersionInfo[];
extern const char kRequestVariationInfo[];
extern const char kRequestPathInfo[];

extern const char kKeyVariationsList[];
extern const char kKeyVariationsCmd[];
extern const char kKeyExecPath[];
extern const char kKeyProfilePath[];

// Strings.
// Must match the constants used in the resource files.
extern const char kApplicationLabel[];
extern const char kCL[];
extern const char kCommandLine[];
extern const char kCommandLineName[];
extern const char kCompany[];
extern const char kCopyright[];
extern const char kExecutablePath[];
extern const char kExecutablePathName[];
extern const char kJSEngine[];
extern const char kJSVersion[];
extern const char kLogoAltText[];
extern const char kOfficial[];
extern const char kOSName[];
extern const char kOSType[];
extern const char kProfilePath[];
extern const char kProfilePathName[];
extern const char kCopyLabel[];
extern const char kCopyNotice[];
extern const char kRevision[];
extern const char kSanitizer[];
extern const char kTitle[];
extern const char kUserAgent[];
extern const char kUserAgentName[];
extern const char kVariationsCmdName[];
extern const char kCopyVariationsLabel[];
extern const char kCopyVariationsNotice[];
extern const char kVariationsName[];
extern const char kVariationsSeed[];
extern const char kVariationsSeedName[];
extern const char kVersion[];
extern const char kVersionSuffix[];
extern const char kVersionModifier[];
extern const char kVersionProcessorVariation[];

#if BUILDFLAG(ENABLE_CEF)
extern const char kKeyModulePath[];
extern const char kKeyUserDataPath[];

extern const char kCefVersion[];
extern const char kModulePath[];
extern const char kModulePathName[];
extern const char kUserDataPath[];
extern const char kUserDataPathName[];
#endif

}  // namespace version_ui

#endif  // COMPONENTS_WEBUI_VERSION_VERSION_UI_CONSTANTS_H_
