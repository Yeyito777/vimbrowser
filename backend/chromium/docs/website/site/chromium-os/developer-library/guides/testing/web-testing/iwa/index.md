---
breadcrumbs:
- - /chromium-os/developer-library/guides/testing
  - Testing
- - /chromium-os/developer-library/guides/testing/web-testing
  - Web Testing
page_name: iwa-testing
title: Testing Isolated Web Apps (IWAs)
---

This guide provides framework-specific examples for testing an Isolated Web App
(IWA) on a ChromeOS device. The tests demonstrate how to install an IWA from a
web bundle, launch it, and interact with its UI.

## Prerequisites

Ensure you have completed the setup steps in the main
**[Web Testing on CrOS](/chromium-os/developer-library/guides/testing/web-testing)** guide.

Additionally, IWA testing requires the following flags to be added to your
`/etc/chrome_dev.conf` file on the CrOS device:
*   `--enable-devtools-pwa-handler`
*   `--enable-features=IsolatedWebAppDevMode`

## Example: IWA Interaction Test

This test showcases how to interact with the Kitchen Sink IWA. The script
installs the IWA from its web bundle, launches it, and then interacts with the
app's UI to simulate sending and receiving messages via direct sockets.

### Puppeteer Example

The `test-iwa-interaction.js` script uses Puppeteer to control the IWA and its
internal UI components.

You can view the full script content here:
[test-iwa-interaction.js](./test-iwa-interaction.js).

**To run the test:**

(host)
```bash
$ node test-iwa-interaction.js ${PORT}
```

### Selenium Example

The `test-iwa-interaction.py` script replicates the same IWA interaction test
using Selenium WebDriver.

You can view the full script content here:
[test-iwa-interaction.py](./test-iwa-interaction.py).

**To run the test:**

(host)
```bash
$ python test-iwa-interaction.py --driver-path ./chromedriver --port ${PORT}
```

### Playwright Example

The `test-iwa-interaction.ts` script uses Playwright to control the IWA and its
internal UI components via CDP commands.

You can view the full script content here:
[test-iwa-interaction.ts](./test-iwa-interaction.ts).

**To run the test:**

(host)
```bash
$ npx ts-node test-iwa-interaction.ts ${PORT}
```
