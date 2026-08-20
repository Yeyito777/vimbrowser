// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_CHROME_RENDER_THREAD_OBSERVER_H_
#define CHROME_RENDERER_CHROME_RENDER_THREAD_OBSERVER_H_

#include <memory>

#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/thread_annotations.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/common/renderer_configuration.mojom.h"
#include "components/content_settings/common/content_settings_manager.mojom.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/signin/public/base/signin_buildflags.h"
#include "content/public/renderer/render_thread_observer.h"
#include "mojo/public/cpp/bindings/associated_receiver_set.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"


#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
class BoundSessionRequestThrottledInRendererManager;
class BoundSessionRequestThrottledHandler;
#endif

namespace visitedlink {
class VisitedLinkReader;
}

// This class filters the incoming control messages (i.e. ones not destined for
// a RenderView) for Chrome specific messages that the content layer doesn't
// happen.  If a few messages are related, they should probably have their own
// observer.
class ChromeRenderThreadObserver : public content::RenderThreadObserver,
                                   public chrome::mojom::RendererConfiguration {
 public:

  ChromeRenderThreadObserver();

  ChromeRenderThreadObserver(const ChromeRenderThreadObserver&) = delete;
  ChromeRenderThreadObserver& operator=(const ChromeRenderThreadObserver&) =
      delete;

  ~ChromeRenderThreadObserver() override;

  // Return a copy of the dynamic parameters - those that may change while the
  // render process is running.
  chrome::mojom::DynamicParamsPtr GetDynamicParams() const;

#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
  std::unique_ptr<BoundSessionRequestThrottledHandler>
  CreateBoundSessionRequestThrottledHandler() const;
#endif

  visitedlink::VisitedLinkReader* visited_link_reader() {
    return visited_link_reader_.get();
  }


  content_settings::mojom::ContentSettingsManager* content_settings_manager() {
    if (content_settings_manager_)
      return content_settings_manager_.get();
    return nullptr;
  }

 private:
  // content::RenderThreadObserver:
  void RegisterMojoInterfaces(
      blink::AssociatedInterfaceRegistry* associated_interfaces) override;
  void UnregisterMojoInterfaces(
      blink::AssociatedInterfaceRegistry* associated_interfaces) override;

  // chrome::mojom::RendererConfiguration:
  void SetInitialConfiguration(
      bool is_incognito_process,
      mojo::PendingReceiver<chrome::mojom::ChromeOSListener>
          chromeos_listener_receiver,
      mojo::PendingRemote<content_settings::mojom::ContentSettingsManager>
          content_settings_manager,
      mojo::PendingRemote<chrome::mojom::BoundSessionRequestThrottledHandler>
          bound_session_request_throttled_handler) override;
  void SetConfiguration(chrome::mojom::DynamicParamsPtr params) override;
  void SetConfigurationOnProcessLockUpdate(
      chrome::mojom::StaticParamsPtr params) override;
  void OnRendererConfigurationAssociatedRequest(
      mojo::PendingAssociatedReceiver<chrome::mojom::RendererConfiguration>
          receiver);

  mojo::Remote<content_settings::mojom::ContentSettingsManager>
      content_settings_manager_;

  std::unique_ptr<visitedlink::VisitedLinkReader> visited_link_reader_;

  mojo::AssociatedReceiverSet<chrome::mojom::RendererConfiguration>
      renderer_configuration_receivers_;

  chrome::mojom::DynamicParamsPtr dynamic_params_
      GUARDED_BY(dynamic_params_lock_);
  mutable base::Lock dynamic_params_lock_;

  bool static_renderer_params_set_ = false;


#if BUILDFLAG(ENABLE_BOUND_SESSION_CREDENTIALS)
  scoped_refptr<BoundSessionRequestThrottledInRendererManager>
      bound_session_request_throttled_in_renderer_manager_;
  scoped_refptr<base::SequencedTaskRunner> io_task_runner_;
#endif
};

#endif  // CHROME_RENDERER_CHROME_RENDER_THREAD_OBSERVER_H_
