---
breadcrumbs:
- - /chromium-os/developer-library/guides/testing
  - Testing
- - /chromium-os/developer-library/guides/testing/web-testing
  - Web Testing
page_name: kiosk-testing
title: Testing in Kiosk Mode
---

This guide provides framework-specific examples for testing web applications in
a locked-down Kiosk mode on a ChromeOS device.

## Prerequisites

Ensure you have completed the setup steps in the main
**[Web Testing on CrOS](/chromium-os/developer-library/guides/testing/web-testing)** guide.

Testing in Kiosk mode requires a more advanced setup:
1.  **Device Policies:** Your CrOS device must be configured with policies to
    auto-launch your web app in Kiosk mode. For detailed instructions, refer to
    the [Testing Enterprise Policies with a Local Server](/chromium-os/developer-library/guides/enterprise/local-policy-testing/) guide.
2.  **Additional Chrome Flags:** You must add the following flags to your
    `/etc/chrome_dev.conf` file on the CrOS device.

    **SECURITY WARNING:** These flags significantly reduce device security.
    **Never enable them on a device connected to an untrusted network.**

    *   `--remote-debugging-address=0.0.0.0`: Allows remote connections.
    *   `--force-devtools-available`: Exposes the app's target to the DevTools
        protocol, which is critical for Kiosk mode.

## Example: Testing an IWA in Kiosk Mode

This test demonstrates how to interact with an Isolated Web App (IWA) running
in Kiosk mode. Because Kiosk mode is highly restrictive, the scripts use
lower-level Chrome DevTools Protocol (CDP) commands to find and interact with
the app's UI.

### Puppeteer Example

The `test-iwa-kiosk.js` script uses Puppeteer's CDP session to control the Kiosk
IWA.

You can view the full script content here:
[test-iwa-kiosk.js](./test-iwa-kiosk.js).

**To run this test:**

(host)
```bash
$ node test-iwa-kiosk.js ${PORT}
```

### Selenium Example

The `test-iwa-kiosk.py` script demonstrates how to discover the Kiosk IWA
target using Selenium's CDP execution capabilities.

You can view the full script content here:
[test-iwa-kiosk.py](./test-iwa-kiosk.py).

**To run this test:**

(host)
```bash
$ python test-iwa-kiosk.py --port ${PORT}
```

### Playwright Example

The `test-iwa-kiosk.ts` script uses Playwright to control the Kiosk IWA via
low-level CDP commands tunneled through a browser session.

You can view the full script content here:
[test-iwa-kiosk.ts](./test-iwa-kiosk.ts).

**To run this test:**

(host)
```bash
$ npx ts-node test-iwa-kiosk.ts ${PORT}
```
