---
breadcrumbs:
- - /chromium-os
  - Chromium OS
- - /chromium-os/developer-library
  - Developer Library
- - /chromium-os/developer-library/guides
  - Guides
- - /chromium-os/developer-library/guides/enterprise
  - Enterprise
page_name: local-policy-testing
title: Testing Enterprise Policies with a Local Server
---

This guide explains how to use the `fake_dmserver` toolkit to apply local
user and device policies to a ChromeOS device for testing. These tools are
available starting from build `M144-16481.0.0`.

[TOC]

## Prerequisites

### Get a Test Image

**These tools are only available on test images.** To use them, you will need a
ChromeOS device running a test image. You have two options:

*   VM (Simplest Approach): Use a [Prebuilt VM for Policy Testing](/chromium-os/developer-library/guides/containers/prebuilt-vm-guide/).
For a simpler setup that doesn't require a Chromium checkout, you can use a
prebuilt ChromiumOS VM. This is ideal for partners and developers who need to
policies in a standardized environment.
*   Physical Device: Install a test image on your device by following the steps in the [Installing ChromiumOS on your device section of the Developer Guide](/chromium-os/developer-library/guides/development/developer-guide/#installing-chromiumos-on-your-device).

### One-Time Device Setup

Before you begin, you must disable root filesystem verification on your test
device. This allows you to write the necessary configuration files.

Run these commands in a terminal on your test device (DUT):
```bash
sudo /usr/share/vboot/bin/make_dev_ssd.sh \
  --remove_rootfs_verification --force && sudo reboot
```

After the device reboots, remount the filesystem as read-write for your
current session:
```bash
sudo mount -o remount,rw /
```

There are two primary workflows:

1.  [Automated Usage (Recommended)](#automated-usage-recommended): A simple,
    one-command process using the `orchestrator.py` script.
2.  [Manual Usage (Advanced)](#manual-usage-advanced): A step-by-step
    process that gives you more control over the individual components.

## Automated Usage (Recommended)

This workflow uses the `orchestrator.py` script to automate the entire
process of generating policies, starting the server, and configuring Chrome. It
is the simplest way to get started.

For detailed, step-by-step instructions, please refer to the
[Automated Usage section in the fake_dmserver README](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/components/policy/tools/fake_dmserver/chromeos_instructions.md#2_automated-usage-recommended).

## Manual Usage (Advanced)

This workflow is for users who want more fine-grained control over the
process, allowing you to run each step manually.

For detailed instructions, see the
[Manual Usage section in the fake_dmserver README](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/components/policy/tools/fake_dmserver/chromeos_instructions.md#3_manual-usage-advanced).

## Enrolling a Device for Testing Device Policies

For a device to receive device policy, it must be enrolled for enterprise
management. This typically needs to be done once, before the device is used.

*   **The device's TPM must be clear.** You can check the status by running
```bash
tpm_manager_client status
```
When you run this command, you should look for a field in the output that
indicates the ownership status, such as `is_owned: true` or `is_owned: false`.
If the TPM is already owned, you must clear it.

*   **Warning:** This will powerwash the device.
```bash
crossystem clear_tpm_owner_request=1
echo "fast keepimg" > /mnt/stateful_partition/factory_install_reset
reboot
```

*   **The device must not have a consumer owner.** If you have previously
    logged into the device, you may need to clear the ownership files.
```bash
stop ui
rm -rf /var/lib/devicesettings/*
start ui
```
To perform the actual enrollment, press `Ctrl+Alt+E` on the sign-in screen.
Use the credentials that match the `policy_user` in your `policies.json` file.
If successful, the device will be enrolled and will begin receiving device
policies from your `fake_dmserver`.

## How to Format `policies.json`

The `policies.json` file is the core configuration for defining which policies
the `fake_dmserver` will apply. For a complete guide on the file structure,
all available configuration parameters, and a detailed example, please see the
[How to Set Valid Policies (`policies.json`) section in the `fake_dmserver`
README](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/components/policy/tools/fake_dmserver/chromeos_instructions.md#4_how-to-set-valid-policies).

## Advanced Topics

For more advanced use cases, such as converting existing policies, setting
custom Chrome flags, testing unlanded policies, or local development on the
tools themselves, please refer to the comprehensive `fake_dmserver`
documentation.

*   [Converting Policies from an Existing Device](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/components/policy/tools/fake_dmserver/chromeos_instructions.md#6_converting-existing-policies-from-dump)
*   [Custom Chrome Flags](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/components/policy/tools/fake_dmserver/chromeos_instructions.md#5_custom-chrome-flags)
*   [Troubleshooting](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/components/policy/tools/fake_dmserver/chromeos_instructions.md#7_troubleshooting)
*   [Testing a New (Unlanded) Policy](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/components/policy/tools/fake_dmserver/chromeos_instructions.md#8_testing-a-new-unlanded_policy)
*   [Local Development Workflow](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/components/policy/tools/fake_dmserver/chromeos_instructions.md#9_local-development-workflow)
