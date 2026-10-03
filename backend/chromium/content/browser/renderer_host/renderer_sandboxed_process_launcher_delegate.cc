// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/renderer_sandboxed_process_launcher_delegate.h"

#include <string_view>

#include "base/strings/string_split.h"
#include "build/build_config.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/common/content_client.h"


#if BUILDFLAG(USE_ZYGOTE)
#include "content/public/common/content_switches.h"
#include "content/public/common/zygote/zygote_handle.h"  // nogncheck
#endif

namespace content {

#if BUILDFLAG(USE_ZYGOTE)
ZygoteCommunication* RendererSandboxedProcessLauncherDelegate::GetZygote() {
  const base::CommandLine& browser_command_line =
      *base::CommandLine::ForCurrentProcess();
  base::CommandLine::StringType renderer_prefix =
      browser_command_line.GetSwitchValueNative(switches::kRendererCmdPrefix);
  if (!renderer_prefix.empty())
    return nullptr;
  return GetGenericZygote();
}
#endif  // BUILDFLAG(USE_ZYGOTE)

#if BUILDFLAG(IS_MAC)
bool RendererSandboxedProcessLauncherDelegate::EnableCpuSecurityMitigations() {
  return true;
}
#endif  // BUILDFLAG(IS_MAC)

sandbox::mojom::Sandbox
RendererSandboxedProcessLauncherDelegate::GetSandboxType() {
  return sandbox::mojom::Sandbox::kRenderer;
}


}  // namespace content
