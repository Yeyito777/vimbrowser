// Copyright 2019 The Crashpad Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef CRASHPAD_UTIL_PROCESS_PROCESS_ID_H_
#define CRASHPAD_UTIL_PROCESS_PROCESS_ID_H_

#include <type_traits>

#include "base/format_macros.h"
#include "build/build_config.h"

#include <sys/types.h>

namespace crashpad {

#if BUILDFLAG(IS_POSIX) || DOXYGEN
//! \brief Alias for platform-specific type to represent a process.
using ProcessID = pid_t;
constexpr ProcessID kInvalidProcessID = -1;
static_assert(std::is_same<ProcessID, int>::value, "Port.");
#define PRI_PROCESS_ID "d"
#else
#error Port.
#endif

}  // namespace crashpad

#endif  // CRASHPAD_UTIL_PROCESS_PROCESS_ID_H_
