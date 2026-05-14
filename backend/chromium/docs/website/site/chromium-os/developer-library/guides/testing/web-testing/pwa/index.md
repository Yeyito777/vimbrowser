---
breadcrumbs:
- - /chromium-os/developer-library/guides/testing
  - Testing
- - /chromium-os/developer-library/guides/testing/web-testing
  - Web Testing
page_name: pwa-testing
title: Testing Progressive Web Apps (PWAs)
---

This guide provides framework-specific examples for testing the lifecycle of a
Progressive Web App (PWA) on a ChromeOS device. The tests demonstrate how to
programmatically install, launch, inspect, and uninstall a PWA.

## Prerequisites

Ensure you have completed the setup steps in the main
**[Web Testing on CrOS](/chromium-os/developer-library/guides/testing/web-testing)** guide.

Additionally, PWA testing requires the `--enable-devtools-pwa-handler` flag to
be added to your `/etc/chrome_dev.conf` file on the CrOS device.

## Example: PWA Lifecycle Test

This test demonstrates a complete lifecycle of a PWA, including installation,
launch, state verification, and uninstallation.

### Puppeteer Example

The `test-pwa-lifecycle.js` script uses the Chrome DevTools Protocol (CDP) via
Puppeteer to programmatically manage the PWA.

You can view the full script content here:
[test-pwa-lifecycle.js](./test-pwa-lifecycle.js).

**To run the test:**

(host)
```bash
$ node test-pwa-lifecycle.js ${PORT}
```

### Selenium Example

The `test-pwa-lifecycle.py` script achieves the same PWA lifecycle testing by
executing CDP commands through Selenium's WebDriver.

You can view the full script content here:
[test-pwa-lifecycle.py](./test-pwa-lifecycle.py).

**To run the test:**

(host)
```bash
$ python test-pwa-lifecycle.py --driver-path ./chromedriver --port ${PORT}
```

### Playwright Example

The `test-pwa-lifecycle.ts` script uses Playwright to manage the PWA lifecycle
via CDP commands.

You can view the full script content here:
[test-pwa-lifecycle.ts](./test-pwa-lifecycle.ts).

**To run the test:**

(host)
```bash
$ npx ts-node test-pwa-lifecycle.ts ${PORT}
```
