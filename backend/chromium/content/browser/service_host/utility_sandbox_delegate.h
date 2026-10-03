// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SERVICE_HOST_UTILITY_SANDBOX_DELEGATE_H_
#define CONTENT_BROWSER_SERVICE_HOST_UTILITY_SANDBOX_DELEGATE_H_

#include <optional>

#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "build/build_config.h"
#include "content/common/content_export.h"
#include "content/public/browser/sandboxed_process_launcher_delegate.h"
#include "content/public/common/zygote/zygote_buildflags.h"
#include "sandbox/policy/mojom/sandbox.mojom.h"

#if BUILDFLAG(USE_ZYGOTE)
#include "content/public/common/zygote/zygote_handle.h"
#endif  // BUILDFLAG(USE_ZYGOTE)


#if BUILDFLAG(IS_MAC)
#include "base/mac/process_requirement.h"
#endif  // BUILDFLAG(IS_MAC)

namespace content {
class CONTENT_EXPORT UtilitySandboxedProcessLauncherDelegate
    : public SandboxedProcessLauncherDelegate {
 public:
  UtilitySandboxedProcessLauncherDelegate(sandbox::mojom::Sandbox sandbox_type,
                                          const base::EnvironmentMap& env,
                                          const base::CommandLine& cmd_line);
  ~UtilitySandboxedProcessLauncherDelegate() override;

  sandbox::mojom::Sandbox GetSandboxType() override;


#if BUILDFLAG(USE_ZYGOTE)
  ZygoteCommunication* GetZygote() override;
#endif  // BUILDFLAG(USE_ZYGOTE)

  base::EnvironmentMap GetEnvironment() override;

#if BUILDFLAG(USE_ZYGOTE)
  void SetZygote(ZygoteCommunication* handle);
#endif  // BUILDFLAG(USE_ZYGOTE_HANDLE)

#if BUILDFLAG(IS_MAC)
  std::optional<base::mac::ProcessRequirement> GetProcessRequirement() override;
#endif  // BUILDFLAG(IS_MAC)

 private:
  base::EnvironmentMap env_;


#if BUILDFLAG(USE_ZYGOTE)
  std::optional<raw_ptr<ZygoteCommunication>> zygote_;
#endif  // BUILDFLAG(USE_ZYGOTE)

  const sandbox::mojom::Sandbox sandbox_type_;
  base::CommandLine cmd_line_;
};
}  // namespace content

#endif  // CONTENT_BROWSER_SERVICE_HOST_UTILITY_SANDBOX_DELEGATE_H_
