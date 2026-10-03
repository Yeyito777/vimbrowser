// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SANDBOX_POLICY_SANDBOX_DELEGATE_H_
#define SANDBOX_POLICY_SANDBOX_DELEGATE_H_

#include <optional>
#include <string>

#include "base/process/process.h"
#include "build/build_config.h"

namespace sandbox {
namespace mojom {
enum class Sandbox;
}  // namespace mojom

class TargetConfig;
class TargetPolicy;

namespace policy {

class SandboxDelegate {
 public:
  virtual ~SandboxDelegate() {}

  // Returns the Sandbox to enforce on the process, or
  // Sandbox::kNoSandbox to run without a sandbox policy.
  virtual sandbox::mojom::Sandbox GetSandboxType() = 0;

};

}  // namespace policy
}  // namespace sandbox

#endif  // SANDBOX_POLICY_SANDBOX_DELEGATE_H_
