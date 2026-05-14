---
breadcrumbs:
- - /chromium-os/developer-library/guides/testing
  - Testing
- - /chromium-os/developer-library/guides/testing/web-testing
  - Web Testing
page_name: browser-connection-testing
title: Testing Browser Connection
---

This guide provides simple test scripts to verify that your host machine can
successfully connect to the Chrome browser on your CrOS device. Running this
test is a crucial step to ensure your device setup and SSH tunnel are working
correctly before proceeding to more complex testing scenarios.

## Prerequisites

Ensure you have completed all the setup steps in the main
[Web Testing on CrOS](/chromium-os/developer-library/guides/testing/web-testing)
guide. Your SSH tunnel must be active to run these tests.

## Example: Browser Connection Test

This test connects to the remote CrOS instance and retrieves the content of the
`chrome://version` page, printing the browser's command-line flags. This allows
you to verify that remote debugging is enabled and that any other flags you have
configured are correctly applied.

### Puppeteer Example

The `test-chrome-connection.js` script uses `puppeteer.connect()` to attach to
the remote browser.

You can view the full script content here:
[test-chrome-connection.js](./test-chrome-connection.js).

**To run the test:**

(host)
```bash
$ node test-chrome-connection.js ${PORT}
```

### Selenium Example

The `test-chrome-connection.py` script uses Selenium's `debuggerAddress` option
to attach to the remote browser.

You can view the full script content here:
[test-chrome-connection.py](./test-chrome-connection.py).

**To run the test:**

(host)
```bash
$ python test-chrome-connection.py --driver-path ./chromedriver --port ${PORT}
```

### Playwright Example

The `test-chrome-connection.ts` script uses Playwright's
`chromium.connectOverCDP()` to attach to the remote browser.

You can view the full script content here:
[test-chrome-connection.ts](./test-chrome-connection.ts).

**To run the test:**

(host)
```bash
$ npx ts-node test-chrome-connection.ts ${PORT}
```
