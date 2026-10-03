// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/url_request_context_builder_mojo.h"

#include "base/check.h"
#include "build/build_config.h"
#include "net/proxy_resolution/configured_proxy_resolution_service.h"
#include "net/proxy_resolution/pac_file_fetcher_impl.h"
#include "net/proxy_resolution/proxy_config_service.h"
#include "services/network/network_context.h"
#include "services/network/proxy_service_mojo.h"
#include "services/network/public/cpp/features.h"

namespace network {

URLRequestContextBuilderMojo::URLRequestContextBuilderMojo() = default;

URLRequestContextBuilderMojo::~URLRequestContextBuilderMojo() = default;

void URLRequestContextBuilderMojo::SetMojoProxyResolverFactory(
    mojo::PendingRemote<proxy_resolver::mojom::ProxyResolverFactory>
        mojo_proxy_resolver_factory) {
  mojo_proxy_resolver_factory_ = std::move(mojo_proxy_resolver_factory);
}



std::unique_ptr<net::DhcpPacFileFetcher>
URLRequestContextBuilderMojo::CreateDhcpPacFileFetcher(
    net::URLRequestContext* context) {
  return std::make_unique<net::DoNothingDhcpPacFileFetcher>();
}

std::unique_ptr<net::ProxyResolutionService>
URLRequestContextBuilderMojo::CreateProxyResolutionService(
    std::unique_ptr<net::ProxyConfigService> proxy_config_service,
    net::URLRequestContext* url_request_context,
    net::HostResolver* host_resolver,
    net::NetworkDelegate* network_delegate,
    net::NetLog* net_log,
    bool pac_quick_check_enabled) {
  DCHECK(url_request_context);
  DCHECK(host_resolver);


  if (mojo_proxy_resolver_factory_) {
    std::unique_ptr<net::DhcpPacFileFetcher> dhcp_pac_file_fetcher =
        CreateDhcpPacFileFetcher(url_request_context);

    std::unique_ptr<net::PacFileFetcherImpl> pac_file_fetcher;
    pac_file_fetcher = net::PacFileFetcherImpl::Create(url_request_context);
    return CreateConfiguredProxyResolutionServiceUsingMojoFactory(
        std::move(mojo_proxy_resolver_factory_),
        std::move(proxy_config_service), std::move(pac_file_fetcher),
        std::move(dhcp_pac_file_fetcher), host_resolver, net_log,
        pac_quick_check_enabled, network_delegate);
  }

  return net::URLRequestContextBuilder::CreateProxyResolutionService(
      std::move(proxy_config_service), url_request_context, host_resolver,
      network_delegate, net_log, pac_quick_check_enabled);
}

}  // namespace network
