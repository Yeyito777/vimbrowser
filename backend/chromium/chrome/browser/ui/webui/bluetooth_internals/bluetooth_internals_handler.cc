// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/bluetooth_internals/bluetooth_internals_handler.h"

#include <fcntl.h>

#include <optional>
#include <string>

#include "base/feature_list.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "device/bluetooth/adapter.h"
#include "device/bluetooth/bluetooth_adapter_factory.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "url/gurl.h"



namespace {
using content::RenderFrameHost;
using content::WebContents;

}  // namespace

BluetoothInternalsHandler::BluetoothInternalsHandler(
    content::RenderFrameHost* render_frame_host,
    mojo::PendingReceiver<mojom::BluetoothInternalsHandler> receiver)
    : render_frame_host_(*render_frame_host),
      receiver_(this, std::move(receiver)) {
}

BluetoothInternalsHandler::~BluetoothInternalsHandler() {
}

void BluetoothInternalsHandler::GetAdapter(GetAdapterCallback callback) {
  if (device::BluetoothAdapterFactory::IsBluetoothSupported()) {
    device::BluetoothAdapterFactory::Get()->GetAdapter(
        base::BindOnce(&BluetoothInternalsHandler::OnGetAdapter,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
  } else {
    std::move(callback).Run(mojo::NullRemote() /* adapter */);
  }
}

void BluetoothInternalsHandler::GetDebugLogsChangeHandler(
    GetDebugLogsChangeHandlerCallback callback) {
  mojo::PendingRemote<mojom::DebugLogsChangeHandler> handler_remote;
  bool initial_toggle_value = false;


  std::move(callback).Run(std::move(handler_remote), initial_toggle_value);
}

void BluetoothInternalsHandler::OnGetAdapter(
    GetAdapterCallback callback,
    scoped_refptr<device::BluetoothAdapter> adapter) {
  mojo::PendingRemote<bluetooth::mojom::Adapter> pending_adapter;
  mojo::MakeSelfOwnedReceiver(std::make_unique<bluetooth::Adapter>(adapter),
                              pending_adapter.InitWithNewPipeAndPassReceiver());
  std::move(callback).Run(std::move(pending_adapter));
}

void BluetoothInternalsHandler::CheckSystemPermissions(
    CheckSystemPermissionsCallback callback) {
  bool need_location_permission = false;
  bool need_nearby_devices_permission = false;
  bool need_location_services = false;
  bool can_request_system_permissions = false;


  std::move(callback).Run(
      need_location_permission, need_nearby_devices_permission,
      need_location_services, can_request_system_permissions);
}

void BluetoothInternalsHandler::RequestSystemPermissions(
    RequestSystemPermissionsCallback callback) {
  std::move(callback).Run();
}

void BluetoothInternalsHandler::RequestLocationServices(
    RequestLocationServicesCallback callback) {
  std::move(callback).Run();
}


void BluetoothInternalsHandler::StartBtsnoop(StartBtsnoopCallback callback) {
  std::move(callback).Run(mojo::NullRemote());
}

void BluetoothInternalsHandler::IsBtsnoopFeatureEnabled(
    IsBtsnoopFeatureEnabledCallback callback) {
  std::move(callback).Run(false);
}
