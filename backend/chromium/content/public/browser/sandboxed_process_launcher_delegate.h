// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_SANDBOXED_PROCESS_LAUNCHER_DELEGATE_H_
#define CONTENT_PUBLIC_BROWSER_SANDBOXED_PROCESS_LAUNCHER_DELEGATE_H_

#include <optional>
#include <string>

#include "base/environment.h"
#include "base/files/scoped_file.h"
#include "base/process/process.h"
#include "build/build_config.h"
#include "content/common/content_export.h"
#include "content/public/common/zygote/zygote_buildflags.h"
#include "sandbox/policy/sandbox_delegate.h"

#if BUILDFLAG(USE_ZYGOTE)
#include "content/public/common/zygote/zygote_handle.h"  // nogncheck
#endif  // BUILDFLAG(USE_ZYGOTE)

#if BUILDFLAG(IS_MAC)
#include "base/mac/process_requirement.h"
#endif  // BUILDFLAG(IS_MAC)

namespace content {

// Allows a caller of StartSandboxedProcess or
// BrowserChildProcessHost/ChildProcessLauncher to control the sandbox policy,
// i.e. to loosen it if needed.
// The methods below will be called on the PROCESS_LAUNCHER thread.
class CONTENT_EXPORT SandboxedProcessLauncherDelegate
    : public sandbox::policy::SandboxDelegate {
 public:
  ~SandboxedProcessLauncherDelegate() override {}



#if BUILDFLAG(USE_ZYGOTE)
  // Returns the zygote used to launch the process.
  virtual ZygoteCommunication* GetZygote();
#endif  // BUILDFLAG(USE_ZYGOTE)

  // Override this if the process needs a non-empty environment map.
  virtual base::EnvironmentMap GetEnvironment();

#if BUILDFLAG(IS_MAC)
  // Whether or not to disclaim TCC responsibility for the process, defaults to
  // false. See base::LaunchOptions::disclaim_responsibility.
  virtual bool DisclaimResponsibility();

  // Whether or not to enable CPU security mitigations against side-channel
  // attacks. See base::LaunchOptions::enable_cpu_security_mitigations.
  virtual bool EnableCpuSecurityMitigations();

  // A `ProcessRequirement` that the launched process will be validated against
  // before it can retrieve any Mach ports and bootstrap Mojo IPC.
  virtual std::optional<base::mac::ProcessRequirement> GetProcessRequirement();
#endif  // BUILDFLAG(IS_MAC)
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_SANDBOXED_PROCESS_LAUNCHER_DELEGATE_H_
