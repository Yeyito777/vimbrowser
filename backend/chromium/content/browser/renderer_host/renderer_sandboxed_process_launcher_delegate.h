// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_RENDERER_SANDBOXED_PROCESS_LAUNCHER_DELEGATE_H_
#define CONTENT_BROWSER_RENDERER_HOST_RENDERER_SANDBOXED_PROCESS_LAUNCHER_DELEGATE_H_

#include "base/command_line.h"
#include "build/build_config.h"
#include "content/common/content_export.h"
#include "content/public/browser/sandboxed_process_launcher_delegate.h"
#include "sandbox/policy/mojom/sandbox.mojom.h"

namespace content {
// NOTE: changes to this class need to be reviewed by the security team.
class CONTENT_EXPORT RendererSandboxedProcessLauncherDelegate
    : public SandboxedProcessLauncherDelegate {
 public:
  RendererSandboxedProcessLauncherDelegate() = default;

  ~RendererSandboxedProcessLauncherDelegate() override = default;

#if BUILDFLAG(USE_ZYGOTE)
  ZygoteCommunication* GetZygote() override;
#endif  // BUILDFLAG(USE_ZYGOTE)

#if BUILDFLAG(IS_MAC)
  bool EnableCpuSecurityMitigations() override;
#endif  // BUILDFLAG(IS_MAC)

  // sandbox::policy::SandboxDelegate:
  sandbox::mojom::Sandbox GetSandboxType() override;
};


}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_RENDERER_SANDBOXED_PROCESS_LAUNCHER_DELEGATE_H_
