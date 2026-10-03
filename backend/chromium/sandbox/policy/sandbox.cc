// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox/policy/sandbox.h"

#include "base/command_line.h"
#include "base/functional/callback.h"
#include "base/metrics/histogram_functions.h"
#include "build/build_config.h"
#include "sandbox/policy/mojom/sandbox.mojom.h"
#include "sandbox/policy/switches.h"


#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
#include "sandbox/policy/linux/sandbox_linux.h"
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_MAC)
#include "sandbox/mac/seatbelt.h"
#endif  // BUILDFLAG(IS_MAC)


namespace sandbox {
namespace policy {

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
bool Sandbox::Initialize(sandbox::mojom::Sandbox sandbox_type,
                         SandboxLinux::PreSandboxHook hook,
                         const SandboxLinux::Options& options) {
  return SandboxLinux::GetInstance()->InitializeSandbox(
      sandbox_type, std::move(hook), options);
}
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)


// static
bool Sandbox::IsProcessSandboxed() {
  auto* command_line = base::CommandLine::ForCurrentProcess();
  bool is_browser = !command_line->HasSwitch(switches::kProcessType);

  if (!is_browser &&
      base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kNoSandbox)) {
    // When running with --no-sandbox, unconditionally report the process as
    // sandboxed. This lets code write |DCHECK(IsProcessSandboxed())| and not
    // break when testing with the --no-sandbox switch.
    return true;
  }

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  int status = SandboxLinux::GetInstance()->GetStatus();
  constexpr int kLayer1Flags = SandboxLinux::Status::kSUID |
                               SandboxLinux::Status::kPIDNS |
                               SandboxLinux::Status::kUserNS;
  constexpr int kLayer2Flags =
      SandboxLinux::Status::kSeccompBPF | SandboxLinux::Status::kSeccompTSYNC;
  return (status & kLayer1Flags) != 0 && (status & kLayer2Flags) != 0;
#elif BUILDFLAG(IS_MAC)
  return Seatbelt::IsSandboxed();
#else
  return false;
#endif
}

}  // namespace policy
}  // namespace sandbox
