// Copyright 2026 The vimbrowser Authors. All rights reserved.

#ifndef CEF_INCLUDE_INTERNAL_VIMBROWSER_PRIVATE_API_H_
#define CEF_INCLUDE_INTERNAL_VIMBROWSER_PRIVATE_API_H_
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "include/internal/cef_export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*vimbrowser_element_activation_callback_t)(
    void* user_data,
    int result,
    int match_count);
typedef void (*vimbrowser_control_inspection_callback_t)(
    void* user_data,
    int result,
    const char* json,
    size_t json_size);

CEF_EXPORT bool vimbrowser_frame_is_out_of_process(
    int browser_id,
    const char* frame_identifier,
    size_t frame_identifier_size);

CEF_EXPORT bool vimbrowser_inspect_frame_controls(
    int browser_id,
    const char* frame_identifier,
    size_t frame_identifier_size,
    const char* role,
    size_t role_size,
    const char* exact_name,
    size_t exact_name_size,
    const char* context_contains,
    size_t context_contains_size,
    uint32_t limit,
    vimbrowser_control_inspection_callback_t callback,
    void* user_data);

CEF_EXPORT bool vimbrowser_activate_element_handle(
    int browser_id,
    const char* handle,
    size_t handle_size,
    bool grant_user_activation,
    uint64_t* activation_nonce_high,
    uint64_t* activation_nonce_low,
    vimbrowser_element_activation_callback_t callback,
    void* user_data);

#ifdef __cplusplus
}
#endif

#endif  // CEF_INCLUDE_INTERNAL_VIMBROWSER_PRIVATE_API_H_
