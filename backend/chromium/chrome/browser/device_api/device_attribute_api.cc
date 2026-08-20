// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/device_api/device_attribute_api.h"

#include "base/functional/callback.h"
#include "build/build_config.h"


using blink::mojom::DeviceAPIService;
using blink::mojom::DeviceAttributeResultPtr;

namespace {

using Result = blink::mojom::DeviceAttributeResult;

constexpr char kNotAffiliatedErrorMessage[] =
    "This web API is not allowed if the current profile is not affiliated.";

constexpr char kNoDeviceAttributesPermissionErrorMessage[] =
    "The current origin cannot use this web API because it was not granted the "
    "'device-attributes' permission.";

const char kNotSupportedPlatformErrorMessage[] =
    "This web API is not supported on the current platform.";

}  // namespace

DeviceAttributeApiImpl::DeviceAttributeApiImpl() = default;
DeviceAttributeApiImpl::~DeviceAttributeApiImpl() = default;

void DeviceAttributeApiImpl::ReportNotAffiliatedError(
    base::OnceCallback<void(DeviceAttributeResultPtr)> callback) {
  std::move(callback).Run(Result::NewErrorMessage(kNotAffiliatedErrorMessage));
}

void DeviceAttributeApiImpl::ReportNotAllowedError(
    base::OnceCallback<void(DeviceAttributeResultPtr)> callback) {
  std::move(callback).Run(
      Result::NewErrorMessage(kNoDeviceAttributesPermissionErrorMessage));
}

void DeviceAttributeApiImpl::GetDirectoryId(
    DeviceAPIService::GetDirectoryIdCallback callback) {
  std::move(callback).Run(
      Result::NewErrorMessage(kNotSupportedPlatformErrorMessage));
}

void DeviceAttributeApiImpl::GetHostname(
    DeviceAPIService::GetHostnameCallback callback) {
  std::move(callback).Run(
      Result::NewErrorMessage(kNotSupportedPlatformErrorMessage));
}

void DeviceAttributeApiImpl::GetSerialNumber(
    DeviceAPIService::GetSerialNumberCallback callback) {
  std::move(callback).Run(
      Result::NewErrorMessage(kNotSupportedPlatformErrorMessage));
}

void DeviceAttributeApiImpl::GetAnnotatedAssetId(
    DeviceAPIService::GetAnnotatedAssetIdCallback callback) {
  std::move(callback).Run(
      Result::NewErrorMessage(kNotSupportedPlatformErrorMessage));
}

void DeviceAttributeApiImpl::GetAnnotatedLocation(
    DeviceAPIService::GetAnnotatedLocationCallback callback) {
  std::move(callback).Run(
      Result::NewErrorMessage(kNotSupportedPlatformErrorMessage));
}
