---
breadcrumbs:
- - /chromium-os/developer-library/guides
  - ChromiumOS > Guides
- - /chromium-os/developer-library/guides/containers
  - ChromiumOS > Guides > Containers
page_name: prebuilt-vm-guide
title: Running a Prebuilt ChromiumOS VM
---

This guide details how to download a prebuilt ChromiumOS VM image, set up
the necessary environment, and run it using QEMU/KVM. While the general
steps are applicable to various operating systems, this document uses
**Linux** as an example environment.

[TOC]

## 1. Prerequisites: Install Software

Install the necessary packages on your Linux machine. This guide uses `apt` for
Debian-based distributions like Ubuntu. If you are using a different
distribution, use your respective package manager (e.g., `dnf`, `pacman`,
`zypper`).

```bash
sudo apt update
sudo apt install -y qemu-system-x86 qemu-utils ovmf xz-utils
```

-   **qemu-system-x86**: The QEMU emulator.
-   **qemu-utils**: QEMU disk image utilities.
-   **ovmf**: UEFI firmware for QEMU.
-   **xz-utils**: For decompressing .tar.xz archives.

You also need the Google Cloud CLI to download the image. Follow the
[official installation instructions](https://cloud.google.com/sdk/docs/install).

## 2. Set Up KVM

KVM (Kernel-based Virtual Machine) is required for hardware-accelerated
emulation.

### Check Hardware Support & KVM Status

```bash
if [[ -e /dev/kvm ]] && grep -qE '^flags.*(vmx|svm)' /proc/cpuinfo; then
    echo 'KVM appears to be working.'
else
    echo 'KVM not detected or not working. Ensure virtualization is' \
         'enabled in your BIOS/UEFI settings.'
fi
```

This checks for the KVM device and Intel (vmx) or AMD (svm) virtualization
extensions.

### Add User to KVM Group

```bash
sudo adduser $(whoami) kvm
```

**Important**: You may need to start a new shell session (e.g., log out and
log back in, or run `newgrp kvm` in your current shell) for this group change
to take effect.

### Verify KVM Access

```bash
if [[ -r /dev/kvm ]] && [[ -w /dev/kvm ]]; then
    echo "KVM access for $(whoami) is OK."
else
    echo "KVM access for $(whoami) FAILED. Check group membership and" \
         "permissions."
fi
```

## 3. Download and Extract the Image

Set the desired ChromiumOS version (e.g., `R144-16481.0.0`):

```bash
export CROS_VERSION="R144-16481.0.0"
```

Find other versions in the Cloud Storage bucket:
<https://console.cloud.google.com/storage/browser/chromiumos-image-archive/amd64-generic-vm-public/>

Define local directories and filenames:

```bash
export IMAGE_DIR="${HOME}/cros_vms/${CROS_VERSION}"
mkdir -p "${IMAGE_DIR}"
export ARCHIVE_FILE="${IMAGE_DIR}/chromiumos_test_image.tar.xz"
export IMAGE_FILE="${IMAGE_DIR}/chromiumos_test_image.bin"
```

Download the image archive using gsutil:

```bash
gsutil cp \
  "gs://chromiumos-image-archive/amd64-generic-vm-public/${CROS_VERSION}/chromiumos_test_image.tar.xz" \
  "${ARCHIVE_FILE}"
```

Extract the `chromiumos_test_image.bin` image into the versioned directory:

```bash
tar -xf "${ARCHIVE_FILE}" -C "${IMAGE_DIR}/"
```

## 4. Launching the VM with QEMU

This will launch the VM in the background.

```bash
qemu-system-x86_64 \
  -m 16G \
  -smp 4 \
  -enable-kvm \
  -cpu host,+ssse3,+sse4.1,+sse4.2,+aes,+pclmulqdq,+popcnt \
  -device virtio-rng-pci \
  -device virtio-scsi-pci,id=scsi \
  -device scsi-hd,drive=hd,bootindex=0 \
  -drive if=none,id=hd,file="${IMAGE_FILE}",format=raw,cache=unsafe \
  -device virtio-net,netdev=user0 \
  -netdev user,id=user0,hostfwd=tcp::2222-:22 \
  -vga virtio \
  -display gtk,show-cursor=on \
  -device usb-ehci,id=ehci \
  -device usb-tablet \
  -device virtio-keyboard-pci
```

## 5. Enabling Sign-In and Google Services (API Keys)

To sign into the VM with a Google account, you need to provide Google API
keys.

For SSH key authentication setup, refer to the
[Optional: Setup SSH Key Authentication](/chromium-os/developer-library/getting-started/setup-chromebook/#optional-setup-ssh-key-authentication)
guide.

First, you need to disable rootfs verification to be able to modify files on
the system partition.

SSH into the running VM (password: `test0000`):

```bash
ssh -i ~/.ssh/testing_rsa -p 2222 -o StrictHostKeyChecking=no \
  root@localhost \
  '/usr/share/vboot/bin/make_dev_ssd.sh --remove_rootfs_verification --force'
```

Reboot the VM:

```bash
ssh -i ~/.ssh/testing_rsa -p 2222 -o StrictHostKeyChecking=no \
  root@localhost 'reboot'
```

Wait for about 30 seconds for the VM to reboot.

### Add Keys to the VM

Remount the root filesystem as read-write:

```bash
ssh -i ~/.ssh/testing_rsa -p 2222 -o StrictHostKeyChecking=no \
  root@localhost 'mount -o remount,rw /'
```

To get API keys, follow the [guide](/developers/how-tos/api-keys/).
Once you have your keys, append them to `/etc/chrome_dev.conf`:

```bash
ssh -i ~/.ssh/testing_rsa -p 2222 -o StrictHostKeyChecking=no \
  root@localhost "echo '
GOOGLE_API_KEY=YOUR_API_KEY
GOOGLE_DEFAULT_CLIENT_ID=YOUR_CLIENT_ID.apps.googleusercontent.com
GOOGLE_DEFAULT_CLIENT_SECRET=YOUR_CLIENT_SECRET
' >> /etc/chrome_dev.conf"
```

and restart the UI:

```bash
ssh -i ~/.ssh/testing_rsa -p 2222 -o StrictHostKeyChecking=no \
  root@localhost 'restart ui'
```

After the UI restarts, you should be able to sign in with a Google account.