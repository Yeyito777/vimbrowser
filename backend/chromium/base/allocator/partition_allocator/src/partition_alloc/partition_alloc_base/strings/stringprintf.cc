// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40284755): Remove this and spanify to fix the errors.
#pragma allow_unsafe_buffers
#endif

#include "partition_alloc/partition_alloc_base/strings/stringprintf.h"

#include <cstdarg>
#include <cstdio>

#include "partition_alloc/partition_alloc_base/compiler_specific.h"
#include "partition_alloc/partition_alloc_base/scoped_clear_last_error.h"

namespace partition_alloc::internal::base {

PA_PRINTF_FORMAT(1, 2)
std::string TruncatingStringPrintf(const char* format, ...) {
  base::ScopedClearLastError last_error;
  char stack_buf[kMaxLengthOfTruncatingStringPrintfResult + 1];
  va_list arguments;
  va_start(arguments, format);
  int result = vsnprintf(stack_buf, std::size(stack_buf), format, arguments);
  va_end(arguments);
  // If an output error is encountered, a negative value is returned.
  // In the case, return an empty string.
  if (result < 0) {
    return std::string();
  }
  // If result is equal or larger than std::size(stack_buf), the output was
  // truncated. ::base::StringPrintf doesn't truncate output.
  return std::string(stack_buf);
}

}  // namespace partition_alloc::internal::base
