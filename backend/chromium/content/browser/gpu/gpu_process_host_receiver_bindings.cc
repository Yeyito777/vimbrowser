// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file exposes services from the browser process to the GPU process.

#include "content/browser/gpu/gpu_process_host.h"

#include "build/build_config.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/common/content_client.h"
#include "mojo/public/cpp/bindings/generic_pending_receiver.h"
#include "services/service_manager/public/cpp/interface_provider.h"



namespace content {

namespace {


}  // namespace

void GpuProcessHost::BindHostReceiver(
    mojo::GenericPendingReceiver generic_receiver) {


  GetContentClient()->browser()->BindGpuHostReceiver(
      std::move(generic_receiver));
}

}  // namespace content
