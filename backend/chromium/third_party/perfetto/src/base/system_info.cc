/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "perfetto/ext/base/system_info.h"

#include "perfetto/base/build_config.h"

#include <string>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/string_utils.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) &&  \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_NACL) && \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM)
#include <sys/utsname.h>
#include <unistd.h>
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX_BUT_NOT_QNX)
#include <sys/sysinfo.h>
#endif

namespace perfetto {
namespace base {

Utsname GetUtsname() {
  Utsname utsname_info;
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) &&  \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_NACL) && \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM)
  struct utsname uname_info;
  if (uname(&uname_info) == 0) {
    utsname_info.sysname = uname_info.sysname;
    utsname_info.version = uname_info.version;
    utsname_info.machine = uname_info.machine;
    utsname_info.release = uname_info.release;
  } else {
    PERFETTO_ELOG("Unable to read Utsname information");
  }
#endif  // !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  return utsname_info;
}

SystemInfo GetSystemInfo() {
  SystemInfo info;

  info.timezone_off_mins = GetTimezoneOffsetMins();

#if !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN) &&  \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_NACL) && \
    !PERFETTO_BUILDFLAG(PERFETTO_OS_WASM)

  info.utsname_info = GetUtsname();
  info.page_size = static_cast<uint32_t>(sysconf(_SC_PAGESIZE));
  info.num_cpus = static_cast<uint32_t>(sysconf(_SC_NPROCESSORS_CONF));

#if PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX_BUT_NOT_QNX)
  // Use the Linux-specific sysinfo() system call on Linux.
  // https://man7.org/linux/man-pages/man2/sysinfo.2.html
  struct sysinfo sys_info;
  if (sysinfo(&sys_info) == 0) {
    info.system_ram_bytes =
        static_cast<uint64_t>(sys_info.totalram) * sys_info.mem_unit;
  }
#else
  // POSIX Fallback (macOS, BSD, etc.): Use sysconf() to get physical pages.
  long pages = sysconf(_SC_PHYS_PAGES);
  if (pages > 0 && info.page_size.has_value()) {
    info.system_ram_bytes = static_cast<uint64_t>(pages) * (*info.page_size);
  }
#endif
#endif  // !PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  return info;
}

std::string GetPerfettoMachineName() {
  const char* env_name = getenv("PERFETTO_MACHINE_NAME");
  if (env_name) {
    return env_name;
  }
  return GetUtsname().sysname;
}

}  // namespace base
}  // namespace perfetto
