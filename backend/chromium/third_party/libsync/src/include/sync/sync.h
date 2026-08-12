/*
 * Copyright 2017 The Android Open Source Project
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

#ifndef THIRD_PARTY_LIBSYNC_INCLUDE_SYNC_SYNC_H_
#define THIRD_PARTY_LIBSYNC_INCLUDE_SYNC_SYNC_H_

#include <stdint.h>

#include <linux/sync_file.h>

#ifdef __cplusplus
extern "C" {
#endif

int sync_wait(int fd, int timeout);
int32_t sync_merge(const char* name, int32_t fd1, int32_t fd2);
struct sync_file_info* sync_file_info(int32_t fd);

static inline struct sync_fence_info* sync_get_fence_info(
    const struct sync_file_info* info) {
#ifdef __cplusplus
  return reinterpret_cast<struct sync_fence_info*>(
      static_cast<uintptr_t>(info->sync_fence_info));
#else
  return (struct sync_fence_info*)(uintptr_t)(info->sync_fence_info);
#endif
}

void sync_file_info_free(struct sync_file_info* info);

#ifdef __cplusplus
}
#endif

#endif  // THIRD_PARTY_LIBSYNC_INCLUDE_SYNC_SYNC_H_
