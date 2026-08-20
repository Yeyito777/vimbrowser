// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/proxy_service_factory.h"

#include <utility>

#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "components/proxy_config/pref_proxy_config_tracker_impl.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "net/proxy_resolution/proxy_config_service.h"


using content::BrowserThread;

// static
std::unique_ptr<net::ProxyConfigService>
ProxyServiceFactory::CreateProxyConfigService(PrefProxyConfigTracker* tracker,
                                              Profile* profile) {
  // The linux gsettings-based proxy settings getter relies on being initialized
  // from the UI thread. The system proxy config service could also get created
  // without full browser process by launching service manager alone.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         !BrowserThread::IsThreadInitialized(BrowserThread::UI));

  std::unique_ptr<net::ProxyConfigService> base_service;

  // On ChromeOS, base service is NULL; ash::ProxyConfigServiceImpl
  // determines the effective proxy config to take effect in the network layer,
  // be it from prefs or system (which is network shill on chromeos).

  // For other platforms, create a baseline service that provides proxy
  // configuration in case nothing is configured through prefs (Note: prefs
  // include command line and configuration policy).

  base_service = net::ProxyConfigService::CreateSystemProxyConfigService(
      base::SingleThreadTaskRunner::GetCurrentDefault());

  return tracker->CreateTrackingProxyConfigService(std::move(base_service));
}

// static
std::unique_ptr<PrefProxyConfigTracker>
ProxyServiceFactory::CreatePrefProxyConfigTrackerOfProfile(
    PrefService* profile_prefs,
    PrefService* local_state_prefs) {
  return std::make_unique<PrefProxyConfigTrackerImpl>(profile_prefs, nullptr);
}

// static
std::unique_ptr<PrefProxyConfigTracker>
ProxyServiceFactory::CreatePrefProxyConfigTrackerOfLocalState(
    PrefService* local_state_prefs) {
  return std::make_unique<PrefProxyConfigTrackerImpl>(local_state_prefs,
                                                      nullptr);
}
