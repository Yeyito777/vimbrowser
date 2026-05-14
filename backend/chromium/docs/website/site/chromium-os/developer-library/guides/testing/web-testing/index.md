---
breadcrumbs:
- - /chromium-os
  - Chromium OS
- - /chromium-os/developer-library
  - Developer Library
- - /chromium-os/developer-library/guides
  - Guides
- - /chromium-os/developer-library/guides/testing
  - Testing
page_name: web-apps-testing-on-cros
title: Web Apps Testing on CrOS
---

This guide is the central starting point for running automated web tests on a
CrOS Device or VM from a separate host machine. It covers the initial device
setup, connection verification, and framework-specific configuration.

[TOC]

## Typography Conventions

Commands are shown with different labels to indicate where they apply to:
1. Your **host machine** (the machine on which you're doing development,
   e.g., Linux, macOS, or Windows).
2. Your **CrOS device or VM** (the device on which you are testing your web
   apps).

| Label | Commands |
| --- | --- |
| (host) | on your host machine |
| (device) | on your CrOS device or VM |

Beneath the label, the command(s) you should type are prefixed with a generic
shell prompt, `$`. This distinguishes input from the output of commands.

## Step 1: CrOS Device or VM Setup

This one-time setup configures your CrOS device to allow remote control of the
browser. It is required to use a device or VM with at least CrOS version
`M141-16376.0.0`.

**Recommendation: Use a ChromiumOS VM**
Starting your testing with a ChromiumOS VM is highly recommended. This allows
for faster iteration, easier debugging, and a more controlled environment.
Follow the [Running a Prebuilt ChromiumOS VM](/chromium-os/developer-library/guides/containers/prebuilt-vm-guide/) guide.

### a. Enable Developer Mode

The device must be in Developer Mode. This will wipe all local data.
Please refer to the [official documentation for your specific device](/chromium-os/developer-library/guides/device/developer-mode/).

### b. Remove Root File System (rootfs) Verification

To modify Chrome's startup flags, you must disable rootfs verification.

**SECURITY WARNING:** This reduces your device's security. Do not perform these
actions on a device with sensitive information.

(device)
```bash
$ sudo /usr/share/vboot/bin/make_dev_ssd.sh \
  --remove_rootfs_verification --force && sudo reboot
```
After rebooting, remount the filesystem as read-write when you need to make
changes:

(device)
```bash
$ sudo mount -o remount,rw /
```

### c. Enable Chrome DevTools Remote Debugging

Start Chrome with the remote debugging port enabled by editing
`/etc/chrome_dev.conf`.

(device)
```bash
$ export PORT=9222
$ echo "--remote-debugging-port=${PORT}" >> /etc/chrome_dev.conf
```
Reboot the device or restart the UI to apply the changes.

### d. Establish and Verify SSH Tunnel

Create an SSH tunnel from your host to the device to forward the debugging port.
This terminal window must remain open during testing.

(host)
```bash
$ export DEVICE_IP=<your_device_ip>
$ export PORT=9222
$ ssh -L ${PORT}:localhost:${PORT} root@${DEVICE_IP}
```
Verify the tunnel is working by running this command in a new terminal on your
host:

(host)
```bash
$ curl http://localhost:${PORT}/json/version
```
A successful connection will return a JSON object with browser details.

## Step 2: Verify Your Connection

Before configuring a specific framework, run a simple connection test to ensure
your setup is working. This guide provides example scripts for both Puppeteer
and Selenium.

*   [Verifying Your Browser Connection](/chromium-os/developer-library/guides/testing/web-testing/browser-connection)

## Step 3: Host Machine Setup & Framework Configuration

This guide assumes you are already familiar with your chosen testing framework.
The primary difference when testing on CrOS is how you connect to the browser.
Instead of launching a new local instance, you connect to the remote browser
via its debugging port.

### a. Puppeteer

Instead of launching a new, local browser instance with
`puppeteer.launch()`, you will connect to the existing browser on the remote
device using `puppeteer.connect()`, targeting the remote debugging port that is
forwarded through your SSH tunnel.

```javascript
const browser = await puppeteer.connect({
  browserURL: `http://localhost:${PORT}`,
});
```

For complete installation and API details, refer to the
[official Puppeteer documentation](https://pptr.dev/guides/installation).

### b. Selenium WebDriver

Instead of having Selenium start a new, local browser instance,
you will configure your WebDriver `Options` to connect to the existing browser
on the remote device by setting the `debuggerAddress`. This targets the remote
debugging port that is forwarded through your SSH tunnel.

```python
options = Options()
options.add_experimental_option("debuggerAddress", f"127.0.0.1:{PORT}")
driver = webdriver.Chrome(service=service, options=options)
```

#### How WebDriver, ChromeDriver, and CDP Work Together

All communication with the browser happens via CDP. The key is understanding
that `chromedriver` acts as a translator.

*   **WebDriver Commands:** These are high-level, standardized commands defined
    by the W3C WebDriver protocol. Your Selenium test script generates these
    commands when you call functions like `driver.get()` or `element.click()`.
    They are generic and work across different browsers (with the appropriate
    driver).

*   **Chrome DevTools Protocol (CDP) Commands:** These are low-level, granular
    commands specific to Chromium-based browsers. They provide deep control over
    the browser's engine for debugging, inspection, and automation.

*   **The `chromedriver` executable:** acts as a bridge it listens for the high
    level WebDriver commands from your script and translates them into the low
    level CDP commands that the Chrome browser on your CrOS device understands.

This flow is illustrated below:

```
+-----------------------------------------------------------+
| Host Machine                                              |
|                                                           |
| +-------------+  WebDriver  +-------------+  CDP Commands |
| | Test Script |-----------> | chromedriver|---+           |
| | (e.g. Py)   |  Commands   | (on Host)   |   |           |
| +-------------+             +-------------+   |           |
|  (High-Level)                (Translates)     |           |
+-----------------------------------------------v-----------+
                                                |
                                                | SSH Tunnel
                                                v
+-----------------------------------------------+-----------+
| CrOS Device                                   |           |
|                                    +----------v-------+   |
|                                    | Chrome Browser   |   |
|                                    | (receives CDP)   |   |
|                                    +------------------+   |
+-----------------------------------------------------------+
```

#### Download & Configure ChromeDriver

Because `chromedriver` must translate commands for a specific version of
Chrome, it is crucial that its version matches the version of Chrome running on
the **CrOS device or VM**.

1.  **Get Chrome Version:** Get the version from the device by either opening
    the `chrome://version` page in the browser or by running the following
    command on the device:

    (device)
    ```bash
    $ /opt/google/chrome/chrome --version
    ```
    You will get a version string like `Google Chrome 144.0.7524.0`.

2.  **Download and Install ChromeDriver:** Visit the
    [Chrome for Testing Dashboard](https://googlechromelabs.github.io/chrome-for-testing/)
    to find the matching driver for your **Host OS** (`linux64`, `mac-x64`, or
    `win64`). If an exact build match isn't available, download the latest
    available build for that Major version. You can either download it manually
    from the dashboard or use a command-line utility like `curl`.

    For example, to download version `144.0.7559.3` on Linux, you can use the
    following commands:

    (host)
    ```bash
    $ curl -o chromedriver.zip https://storage.googleapis.com/chrome-for-testing-public/144.0.7559.3/linux64/chromedriver-linux64.zip
    $ unzip chromedriver.zip
    $ mv chromedriver-linux64/chromedriver .
    $ chmod +x chromedriver
    ```
For more details, see the [official Selenium documentation](https://www.selenium.dev/documentation/).

### c. Playwright

Instead of launching a new, local browser instance with `chromium.launch()`, you
will connect to the existing browser on the remote device using
`chromium.connectOverCDP()`, targeting the remote debugging port that is
forwarded through your SSH tunnel.

```typescript
import { chromium } from 'playwright';

const browser = await chromium.connectOverCDP(`http://localhost:${PORT}`);
```

For complete installation and API details, refer to the
[official Playwright documentation](https://playwright.dev/docs/intro).

## Step 4: Explore Testing Scenarios

Now that you have configured your framework, you can explore task-specific
testing guides that provide concrete examples for different scenarios.

*   [Testing Browser Connection](/chromium-os/developer-library/guides/testing/web-testing/browser-connection)
*   [Testing Progressive Web Apps (PWAs)](/chromium-os/developer-library/guides/testing/web-testing/pwa)
*   [Testing Isolated Web Apps (IWAs)](/chromium-os/developer-library/guides/testing/web-testing/iwa)
*   [Testing System Web Apps (SWAs)](/chromium-os/developer-library/guides/testing/web-testing/swa)
*   [Testing in Kiosk Mode](/chromium-os/developer-library/guides/testing/web-testing/kiosk)
