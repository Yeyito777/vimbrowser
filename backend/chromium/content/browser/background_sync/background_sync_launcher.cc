// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/background_sync/background_sync_launcher.h"

#include <algorithm>
#include <utility>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "content/browser/background_sync/background_sync_context_impl.h"
#include "content/public/browser/background_sync_context.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"

namespace content {

namespace {

base::LazyInstance<BackgroundSyncLauncher>::DestructorAtExit
    g_background_sync_launcher = LAZY_INSTANCE_INITIALIZER;

}  // namespace

// static
BackgroundSyncLauncher* BackgroundSyncLauncher::Get() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  return g_background_sync_launcher.Pointer();
}

// static
base::TimeDelta BackgroundSyncLauncher::GetSoonestWakeupDelta(
    blink::mojom::BackgroundSyncType sync_type,
    BrowserContext* browser_context) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  return Get()->GetSoonestWakeupDeltaImpl(sync_type, browser_context);
}

// static

BackgroundSyncLauncher::BackgroundSyncLauncher() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

BackgroundSyncLauncher::~BackgroundSyncLauncher() = default;

void BackgroundSyncLauncher::SetGlobalSoonestWakeupDelta(
    blink::mojom::BackgroundSyncType sync_type,
    base::TimeDelta set_to) {
  if (sync_type == blink::mojom::BackgroundSyncType::ONE_SHOT)
    soonest_wakeup_delta_one_shot_ = set_to;
  else
    soonest_wakeup_delta_periodic_ = set_to;
}

base::TimeDelta BackgroundSyncLauncher::GetGlobalSoonestWakeupDelta(
    blink::mojom::BackgroundSyncType sync_type) {
  if (sync_type == blink::mojom::BackgroundSyncType::ONE_SHOT)
    return soonest_wakeup_delta_one_shot_;
  else
    return soonest_wakeup_delta_periodic_;
}

base::TimeDelta BackgroundSyncLauncher::GetSoonestWakeupDeltaImpl(
    blink::mojom::BackgroundSyncType sync_type,
    BrowserContext* browser_context) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  SetGlobalSoonestWakeupDelta(sync_type, base::TimeDelta::Max());
  browser_context->ForEachLoadedStoragePartition(
      [&](StoragePartition* partition) {
        GetSoonestWakeupDeltaForStoragePartition(sync_type, partition);
      });

  return GetGlobalSoonestWakeupDelta(sync_type);
}

void BackgroundSyncLauncher::GetSoonestWakeupDeltaForStoragePartition(
    blink::mojom::BackgroundSyncType sync_type,
    StoragePartition* storage_partition) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  BackgroundSyncContextImpl* sync_context =
      static_cast<BackgroundSyncContextImpl*>(
          storage_partition->GetBackgroundSyncContext());
  DCHECK(sync_context);

  base::TimeDelta wakeup_delta = sync_context->GetSoonestWakeupDelta(
      sync_type, last_browser_wakeup_for_periodic_sync_);
  if (wakeup_delta < GetGlobalSoonestWakeupDelta(sync_type))
    SetGlobalSoonestWakeupDelta(sync_type, wakeup_delta);
}

void BackgroundSyncLauncher::SendSoonestWakeupDelta(
    blink::mojom::BackgroundSyncType sync_type,
    base::OnceCallback<void(base::TimeDelta)> callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

}

}  // namespace content
