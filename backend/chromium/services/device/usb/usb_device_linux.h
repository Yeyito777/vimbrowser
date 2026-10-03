// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_DEVICE_USB_USB_DEVICE_LINUX_H_
#define SERVICES_DEVICE_USB_USB_DEVICE_LINUX_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <utility>

#include "base/files/scoped_file.h"
#include "base/sequence_checker.h"
#include "build/build_config.h"
#include "services/device/usb/usb_device.h"

namespace base {
class SequencedTaskRunner;
}

namespace device {

struct UsbDeviceDescriptor;

class UsbDeviceLinux : public UsbDevice {
 public:
  UsbDeviceLinux(const UsbDeviceLinux&) = delete;
  UsbDeviceLinux& operator=(const UsbDeviceLinux&) = delete;

// UsbDevice implementation:
  void Open(OpenCallback callback) override;

  const std::string& device_path() const { return device_path_; }

  // This function is used during enumeration only. The values must not
  // change during the object's lifetime.
  void set_webusb_landing_page(const GURL& url) {
    device_info_->webusb_landing_page = url;
  }

 protected:
  friend class UsbServiceLinux;

  // Called by UsbServiceLinux only.
  UsbDeviceLinux(const std::string& device_path,
                 std::unique_ptr<UsbDeviceDescriptor> descriptor);

  ~UsbDeviceLinux() override;

 private:
  void OpenOnBlockingThread(
      OpenCallback callback,
      scoped_refptr<base::SequencedTaskRunner> task_runner,
      scoped_refptr<base::SequencedTaskRunner> blocking_task_runner);
  void Opened(base::ScopedFD fd,
              base::ScopedFD lifeline_fd,
              const std::string& client_id,
              OpenCallback callback,
              scoped_refptr<base::SequencedTaskRunner> blocking_task_runner);

  SEQUENCE_CHECKER(sequence_checker_);

  const std::string device_path_;
};

}  // namespace device

#endif  // SERVICES_DEVICE_USB_USB_DEVICE_LINUX_H_
