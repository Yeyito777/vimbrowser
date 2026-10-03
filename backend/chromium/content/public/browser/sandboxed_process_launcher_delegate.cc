// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/sandboxed_process_launcher_delegate.h"

#include <optional>
#include <string>

#include "build/build_config.h"
#include "content/public/common/zygote/zygote_buildflags.h"

#if BUILDFLAG(IS_MAC)
#include "base/mac/process_requirement.h"
#endif  // BUILDFLAG(IS_MAC)


namespace content {



#if BUILDFLAG(USE_ZYGOTE)
ZygoteCommunication* SandboxedProcessLauncherDelegate::GetZygote() {
  // Default to the sandboxed zygote. If a more lax sandbox is needed, then the
  // child class should override this method and use the unsandboxed zygote.
  return GetGenericZygote();
}
#endif  // BUILDFLAG(USE_ZYGOTE)

base::EnvironmentMap SandboxedProcessLauncherDelegate::GetEnvironment() {
  return base::EnvironmentMap();
}

#if BUILDFLAG(IS_MAC)

bool SandboxedProcessLauncherDelegate::DisclaimResponsibility() {
  return false;
}

bool SandboxedProcessLauncherDelegate::EnableCpuSecurityMitigations() {
  return false;
}

std::optional<base::mac::ProcessRequirement>
SandboxedProcessLauncherDelegate::GetProcessRequirement() {
  return std::nullopt;
}

#endif  // BUILDFLAG(IS_MAC)

}  // namespace content
