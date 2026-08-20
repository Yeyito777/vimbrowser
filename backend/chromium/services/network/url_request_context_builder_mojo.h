// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_NETWORK_URL_REQUEST_CONTEXT_BUILDER_MOJO_H_
#define SERVICES_NETWORK_URL_REQUEST_CONTEXT_BUILDER_MOJO_H_

#include <memory>

#include "base/component_export.h"
#include "build/build_config.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "net/url_request/url_request_context_builder.h"
#include "services/network/url_request_context_owner.h"
#include "services/proxy_resolver/public/mojom/proxy_resolver.mojom.h"



namespace net {
class DhcpPacFileFetcher;
class HostResolver;
class NetLog;
class NetworkDelegate;
class ProxyResolutionService;
class URLRequestContext;
}  // namespace net

namespace network {
// Specialization of URLRequestContextBuilder that can create one or more
// ProxyResolutionServices that use Mojo. This can be a
// ConfiguredProxyResolutionService that uses a Mojo ProxyResolver or a
// WindowsSystemProxyResolutionService that may mojo all proxy resolutions to a
// utility process if enabled. The consumer is responsible for providing either
// the proxy_resolver::mojom::ProxyResolverFactory or
// proxy_resolver::mojom::SystemProxyResolver respectively. If a
// ProxyResolutionService is set directly via the URLRequestContextBuilder API,
// it will be used instead either of the ProxyResolutionService implementations
// mentioned here.
class COMPONENT_EXPORT(NETWORK_SERVICE) URLRequestContextBuilderMojo
    : public net::URLRequestContextBuilder {
 public:
  URLRequestContextBuilderMojo();

  URLRequestContextBuilderMojo(const URLRequestContextBuilderMojo&) = delete;
  URLRequestContextBuilderMojo& operator=(const URLRequestContextBuilderMojo&) =
      delete;

  ~URLRequestContextBuilderMojo() override;

  // Sets Mojo factory used to create ProxyResolvers. If not set, falls back to
  // URLRequestContext's default behavior.
  void SetMojoProxyResolverFactory(
      mojo::PendingRemote<proxy_resolver::mojom::ProxyResolverFactory>
          mojo_proxy_resolver_factory);



 private:
  std::unique_ptr<net::ProxyResolutionService> CreateProxyResolutionService(
      std::unique_ptr<net::ProxyConfigService> proxy_config_service,
      net::URLRequestContext* url_request_context,
      net::HostResolver* host_resolver,
      net::NetworkDelegate* network_delegate,
      net::NetLog* net_log,
      bool pac_quick_check_enabled) override;

  std::unique_ptr<net::DhcpPacFileFetcher> CreateDhcpPacFileFetcher(
      net::URLRequestContext* context);


  mojo::PendingRemote<proxy_resolver::mojom::ProxyResolverFactory>
      mojo_proxy_resolver_factory_;

};

}  // namespace network

#endif  // SERVICES_NETWORK_URL_REQUEST_CONTEXT_BUILDER_MOJO_H_
