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

#ifndef INCLUDE_PERFETTO_EXT_BASE_SYSTEM_INFO_H_
#define INCLUDE_PERFETTO_EXT_BASE_SYSTEM_INFO_H_

#include <cstdint>
#include <optional>
#include <string>

namespace perfetto {
namespace base {

struct Utsname {
  std::string sysname;
  std::string version;
  std::string machine;
  std::string release;
};

struct SystemInfo {
  std::optional<int32_t> timezone_off_mins;
  std::optional<Utsname> utsname_info;
  std::optional<uint32_t> page_size;
  std::optional<uint32_t> num_cpus;
  std::optional<uint64_t> system_ram_bytes;
};

// Returns the device's utsname information.
Utsname GetUtsname();

// Returns the device's system information.
SystemInfo GetSystemInfo();

// Returns the Perfetto machine name. PERFETTO_MACHINE_NAME has precedence;
// otherwise the OS system name from uname is used.
std::string GetPerfettoMachineName();

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_EXT_BASE_SYSTEM_INFO_H_
