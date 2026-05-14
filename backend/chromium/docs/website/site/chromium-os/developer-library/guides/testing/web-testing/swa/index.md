---
breadcrumbs:
- - /chromium-os/developer-library/guides/testing
  - Testing
- - /chromium-os/developer-library/guides/testing/web-testing
  - Web Testing
page_name: swa-testing
title: Testing System Web Apps (SWAs)
---

This document outlines techniques for automating CrOS system
applications (System Web Apps) such as the Files app, Media App, and Settings.

[TOC]

## Prerequisites

Ensure you have completed the setup steps in the main
**[Web Testing on CrOS](/chromium-os/developer-library/guides/testing/web-testing)** guide.

Additionally, testing System Web Apps requires the
`--enable-devtools-pwa-handler` flag to be enabled and must be added to the
`/etc/chrome_dev.conf` file on the CrOS device.

**Stability Warning:** While Puppeteer and Selenium allow you to interact with
system-level apps, these tests rely on internal DOM structures (often specific
Shadow DOM hierarchies). Unlike public web APIs, ChromeOS system UI can change
frequently, which may break your tests. Selectors should be considered brittle
and may require regular maintenance.

## Finding App Targets

System apps do not appear as standard browser tabs. You must locate the correct
"Target" (window or background page) to attach your testing session.

*   **Manual Inspection:** Open `chrome://inspect/#other` on your host machine
    while connected to the DUT to inspect system app targets and find their
    URLs.
*   **Automation:** Use your framework's API to find the appropriate target by
    filtering by URL and type. For example, in Puppeteer:

    ```javascript
    // Example: Finding the Files App target
    const filesAppTarget = await browser.waitForTarget(
        target => target.url().startsWith('chrome://file-manager') && target.type()
        === 'page'
    );
    ```

## Key Automation Techniques

*   **Launching Apps:** System Web Apps are typically launched via their
    manifest ID using the PWA domain of the Chrome DevTools Protocol (CDP).
    Most frameworks provide a way to execute raw CDP commands.

    ```javascript
    // Example: Launching the Files App via CDP
    await send(null, 'PWA.launch', { manifestId: 'chrome://file-manager' });
    ```

*   **Handling Shadow DOM:** System apps heavily utilize Web Components and Shadow
    DOM. Standard selectors (e.g., `page.$('#id')`) cannot pierce the Shadow DOM
    boundary. You must traverse the `.shadowRoot` property of an element to
    inspect its internal DOM.

    ```javascript
    // Example: Piercing Shadow DOM to find a label in Puppeteer
    const element = await page.waitForFunction(() => {
        const host = document.querySelector('xf-tree-item');
        return host.shadowRoot?.querySelector('span#tree-label');
    });
    ```

## Example: Interacting with the Files App

This test simulates a full user workflow: downloading a file, opening the Files
app, finding the downloaded file, opening it in the Media App, verifying its
content, and then deleting it.

### Puppeteer Example

The `test-file-download.js` script uses Puppeteer and the Chrome DevTools
Protocol (CDP) to automate the Files app and Media app.

You can view the full script content here:
[test-file-download.js](./test-file-download.js).

**To run this test:**

(host)
```bash
$ node test-file-download.js ${PORT}
```

### Selenium Example

The `test-file-download.py` script replicates the same file download and
verification workflow using Selenium WebDriver.

You can view the full script content here:
[test-file-download.py](./test-file-download.py).

**To run this test:**

(host)
```bash
$ python test-file-download.py --driver-path ./chromedriver --port ${PORT}
```

### Playwright Example

The `test-file-download.ts` script uses Playwright to automate the Files app
and Media app, verifying a file download and its content.

You can view the full script content here:
[test-file-download.ts](./test-file-download.ts).

**To run this test:**

(host)
```bash
$ npx ts-node test-file-download.ts ${PORT}
```
