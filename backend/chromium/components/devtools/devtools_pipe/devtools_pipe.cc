// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/devtools/devtools_pipe/devtools_pipe.h"

#include "build/build_config.h"
#include "content/public/browser/devtools_agent_host.h"

#include <errno.h>
#include <fcntl.h>

namespace devtools_pipe {

namespace {


}  // namespace

bool AreFileDescriptorsOpen() {
  return fcntl(content::DevToolsAgentHost::kReadFD, F_GETFL) != -1 &&
         fcntl(content::DevToolsAgentHost::kWriteFD, F_GETFL) != -1;
}

}  // namespace devtools_pipe
