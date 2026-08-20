// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync_device_info/local_device_info_util.h"

#include <string_view>

#include "base/barrier_closure.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/strings/string_util.h"
#include "base/system/sys_info.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "build/build_config.h"
#include "components/sync/protocol/sync_enums.pb.h"
#include "ui/base/device_form_factor.h"


namespace syncer {

// Declared here but defined in platform-specific files.
std::string GetPersonalizableDeviceNameInternal();


LocalDeviceNameInfo::LocalDeviceNameInfo() = default;
LocalDeviceNameInfo::LocalDeviceNameInfo(const LocalDeviceNameInfo& other) =
    default;
LocalDeviceNameInfo::~LocalDeviceNameInfo() = default;

namespace {

void OnLocalDeviceNameInfoReady(
    base::OnceCallback<void(LocalDeviceNameInfo)> callback,
    std::unique_ptr<LocalDeviceNameInfo> name_info) {
  std::move(callback).Run(std::move(*name_info));
}

void OnHardwareInfoReady(LocalDeviceNameInfo* name_info_ptr,
                         base::ScopedClosureRunner done_closure,
                         base::SysInfo::HardwareInfo hardware_info) {
  name_info_ptr->manufacturer_name = std::move(hardware_info.manufacturer);
  name_info_ptr->model_name = std::move(hardware_info.model);
}

void OnPersonalizableDeviceNameReady(LocalDeviceNameInfo* name_info_ptr,
                                     base::ScopedClosureRunner done_closure,
                                     std::string personalizable_name) {
  name_info_ptr->personalizable_name = std::move(personalizable_name);
}

void OnMachineStatisticsLoaded(LocalDeviceNameInfo* name_info_ptr,
                               base::ScopedClosureRunner done_closure) {
  name_info_ptr->full_hardware_class = "";
}

}  // namespace

sync_pb::SyncEnums::DeviceType GetLocalDeviceType() {
#if BUILDFLAG(IS_LINUX)
  return sync_pb::SyncEnums_DeviceType_TYPE_LINUX;
#elif BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
  switch (ui::GetDeviceFormFactor()) {
    case ui::DEVICE_FORM_FACTOR_TABLET:
      return sync_pb::SyncEnums_DeviceType_TYPE_TABLET;
    case ui::DEVICE_FORM_FACTOR_PHONE:
      return sync_pb::SyncEnums_DeviceType_TYPE_PHONE;
    default:
      return sync_pb::SyncEnums_DeviceType_TYPE_OTHER;
  }
#elif BUILDFLAG(IS_MAC)
  return sync_pb::SyncEnums_DeviceType_TYPE_MAC;
#else
  return sync_pb::SyncEnums_DeviceType_TYPE_OTHER;
#endif
}

DeviceInfo::OsType GetLocalDeviceOSType() {
#if BUILDFLAG(IS_LINUX)
  return DeviceInfo::OsType::kLinux;
#elif BUILDFLAG(IS_MAC)
  return DeviceInfo::OsType::kMac;
#else
#error Please handle your new device OS here.
#endif
}

DeviceInfo::FormFactor GetLocalDeviceFormFactor() {
  switch (ui::GetDeviceFormFactor()) {
    case ui::DEVICE_FORM_FACTOR_TABLET:
      return DeviceInfo::FormFactor::kTablet;
    case ui::DEVICE_FORM_FACTOR_DESKTOP:
      return DeviceInfo::FormFactor::kDesktop;
    case ui::DEVICE_FORM_FACTOR_TV:
      return DeviceInfo::FormFactor::kTv;
    case ui::DEVICE_FORM_FACTOR_AUTOMOTIVE:
      return DeviceInfo::FormFactor::kAutomotive;
    case ui::DEVICE_FORM_FACTOR_PHONE:
    case ui::DEVICE_FORM_FACTOR_FOLDABLE:
      return DeviceInfo::FormFactor::kPhone;
    case ui::DEVICE_FORM_FACTOR_XR:
      return DeviceInfo::FormFactor::kUnknown;
  }
  NOTREACHED();
}

std::string GetPersonalizableDeviceNameBlocking() {
  base::ScopedBlockingCall scoped_blocking_call(FROM_HERE,
                                                base::BlockingType::MAY_BLOCK);
  std::string device_name = GetPersonalizableDeviceNameInternal();

  if (device_name == "Unknown" || device_name.empty()) {
    device_name = base::SysInfo::OperatingSystemName();
  }

  DCHECK(base::IsStringUTF8(device_name));
  return device_name;
}

void GetLocalDeviceNameInfo(
    base::OnceCallback<void(LocalDeviceNameInfo)> callback) {
  auto name_info = std::make_unique<LocalDeviceNameInfo>();
  LocalDeviceNameInfo* name_info_ptr = name_info.get();

  auto done_closure = base::BarrierClosure(
      /*num_closures=*/3,
      base::BindOnce(&OnLocalDeviceNameInfoReady, std::move(callback),
                     std::move(name_info)));

  base::SysInfo::GetHardwareInfo(
      base::BindOnce(&OnHardwareInfoReady, name_info_ptr,
                     base::ScopedClosureRunner(done_closure)));

  OnMachineStatisticsLoaded(name_info_ptr,
                            base::ScopedClosureRunner(done_closure));

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&GetPersonalizableDeviceNameBlocking),
      base::BindOnce(&OnPersonalizableDeviceNameReady, name_info_ptr,
                     base::ScopedClosureRunner(done_closure)));
}

}  // namespace syncer
