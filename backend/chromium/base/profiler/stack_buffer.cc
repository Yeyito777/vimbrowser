// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/stack_buffer.h"

#include <bit>

#include "base/compiler_specific.h"


namespace base {

constexpr size_t StackBuffer::kPlatformStackAlignment;


StackBuffer::StackBuffer(size_t buffer_size)
    : size_(buffer_size),
      buffer_(static_cast<uintptr_t*>(
          AlignedAlloc(size_, kPlatformStackAlignment))) {
  static_assert(std::has_single_bit(kPlatformStackAlignment));
}

StackBuffer::~StackBuffer() = default;

}  // namespace base
