// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/device/usb/usb_device_linux.h"

#include <fcntl.h>
#include <stddef.h>

#include <algorithm>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/posix/eintr_wrapper.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "components/device_event_log/device_event_log.h"
#include "services/device/usb/usb_descriptors.h"
#include "services/device/usb/usb_device_handle_usbfs.h"
#include "services/device/usb/usb_service.h"


namespace device {

UsbDeviceLinux::UsbDeviceLinux(const std::string& device_path,
                               std::unique_ptr<UsbDeviceDescriptor> descriptor)
    : UsbDevice(std::move(descriptor->device_info)),
      device_path_(device_path) {}

UsbDeviceLinux::~UsbDeviceLinux() = default;


void UsbDeviceLinux::Open(OpenCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner =
      UsbService::CreateBlockingTaskRunner();
  blocking_task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(&UsbDeviceLinux::OpenOnBlockingThread, this,
                     std::move(callback),
                     base::SingleThreadTaskRunner::GetCurrentDefault(),
                     blocking_task_runner));
}


void UsbDeviceLinux::OpenOnBlockingThread(
    OpenCallback callback,
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    scoped_refptr<base::SequencedTaskRunner> blocking_task_runner) {
  base::ScopedFD fd(HANDLE_EINTR(open(device_path_.c_str(), O_RDWR)));
  // Client id is only used for ChromeOS so pass empty string here to indicate
  // an invalid client id.
  std::string empty_client_id = "";
  if (fd.is_valid()) {
    task_runner->PostTask(
        FROM_HERE, base::BindOnce(&UsbDeviceLinux::Opened, this, std::move(fd),
                                  base::ScopedFD(), empty_client_id,
                                  std::move(callback), blocking_task_runner));
  } else {
    USB_PLOG(EVENT) << "Failed to open " << device_path_;
    task_runner->PostTask(FROM_HERE,
                          base::BindOnce(std::move(callback), nullptr));
  }
}


void UsbDeviceLinux::Opened(
    base::ScopedFD fd,
    base::ScopedFD lifeline_fd,
    const std::string& client_id,
    OpenCallback callback,
    scoped_refptr<base::SequencedTaskRunner> blocking_task_runner) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  scoped_refptr<UsbDeviceHandle> device_handle =
      new UsbDeviceHandleUsbfs(this, std::move(fd), std::move(lifeline_fd),
                               client_id, blocking_task_runner);
  handles().push_back(device_handle.get());
  std::move(callback).Run(device_handle);
}

}  // namespace device
