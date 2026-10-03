// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox/policy/features.h"

#include "base/feature_list.h"
#include "build/build_config.h"
#include "sandbox/features.h"


namespace sandbox::policy::features {

#if !BUILDFLAG(IS_MAC) && !BUILDFLAG(IS_FUCHSIA)
// Enables network service sandbox.
// (Only causes an effect when feature kNetworkServiceInProcess is disabled.)
BASE_FEATURE(kNetworkServiceSandbox, base::FEATURE_DISABLED_BY_DEFAULT);

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
// Enables a fine-grained seccomp-BPF syscall filter for the network service.
// Only has an effect if IsNetworkSandboxEnabled() returns true.
// If the network service sandbox is enabled and |kNetworkServiceSyscallFilter|
// is disabled, a seccomp-BPF filter will still be applied but it will not
// disallow any syscalls.
BASE_FEATURE(kNetworkServiceSyscallFilter, base::FEATURE_ENABLED_BY_DEFAULT);
// Enables a fine-grained file path allowlist for the network service.
// Only has an effect if IsNetworkSandboxEnabled() returns true.
// If the network service sandbox is enabled and |kNetworkServiceFileAllowlist|
// is disabled, a file path allowlist will still be applied, but the policy will
// allow everything.
BASE_FEATURE(kNetworkServiceFileAllowlist, base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
#endif  // !BUILDFLAG(IS_MAC) && !BUILDFLAG(IS_FUCHSIA)



#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
// Increase the renderer sandbox memory limit. As of 2023, there are no limits
// on macOS, and a 1TiB limit on Windows. There are reports of users bumping
// into the limit. This increases the limit by 2x compared to the default
// state. We are not increasing it all the way as on Windows as Linux systems
// typically ship with overcommit, so there is no "commit limit" to save us
// from egregious cases as on Windows.
BASE_FEATURE(kHigherRendererMemoryLimit, base::FEATURE_DISABLED_BY_DEFAULT);

#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)



bool IsNetworkSandboxEnabled() {
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_FUCHSIA)
  return true;
#else
  // Check feature status.
  return base::FeatureList::IsEnabled(kNetworkServiceSandbox);
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_FUCHSIA)
}

}  // namespace sandbox::policy::features
