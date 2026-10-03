// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_SOCKET_DESCRIPTOR_H_
#define NET_SOCKET_SOCKET_DESCRIPTOR_H_

#include "build/build_config.h"
#include "net/base/net_export.h"


namespace net {

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
typedef int SocketDescriptor;
const SocketDescriptor kInvalidSocket = -1;
#endif

// Creates  socket. See WSASocket/socket documentation of parameters.
SocketDescriptor NET_EXPORT CreatePlatformSocket(int family,
                                                 int type,
                                                 int protocol);

}  // namespace net

#endif  // NET_SOCKET_SOCKET_DESCRIPTOR_H_
