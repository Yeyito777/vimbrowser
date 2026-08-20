// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/io_watcher.h"

#include <memory>

#include "base/check.h"
#include "base/no_destructor.h"
#include "base/task/current_thread.h"
#include "build/build_config.h"

namespace base {

IOWatcher::IOWatcher() = default;

IOWatcher* IOWatcher::Get() {
  if (!CurrentThread::IsSet()) {
    return nullptr;
  }
  return CurrentThread::Get()->GetIOWatcher();
}

std::unique_ptr<IOWatcher::FdWatch> IOWatcher::WatchFileDescriptor(
    int fd,
    FdWatchDuration duration,
    FdWatchMode mode,
    FdWatcher& fd_watcher,
    const Location& location) {
  return WatchFileDescriptorImpl(fd, duration, mode, fd_watcher, location);
}

#if BUILDFLAG(IS_MAC) || \
    (BUILDFLAG(IS_IOS) && !BUILDFLAG(CRONET_BUILD) && !BUILDFLAG(IS_IOS_TVOS))
bool IOWatcher::WatchMachReceivePort(
    mach_port_t port,
    MessagePumpForIO::MachPortWatchController* controller,
    MessagePumpForIO::MachPortWatcher* delegate) {
  return WatchMachReceivePortImpl(port, controller, delegate);
}
#endif

}  // namespace base
