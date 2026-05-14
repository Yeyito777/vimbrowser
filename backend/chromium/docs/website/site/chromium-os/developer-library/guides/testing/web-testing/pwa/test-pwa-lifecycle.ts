// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Playwright Test Script for a Standard PWA Lifecycle
 * (TypeScript).
 *
 * This script connects to a remote Chrome instance, installs a standard PWA
 * using the `PWA` Chrome DevTools Protocol (CDP) domain. It then launches the
 * PWA, checks its state, and finally uninstalls it.
 *
 * Prerequisites:
 * 1. A CrOS device or VM setup for remote testing.
 * 2. Chrome must be launched with:
 * - `--remote-debugging-port=<port>`
 * - `--enable-devtools-pwa-handler`
 */

import * as fs from 'fs';
import * as path from 'path';
import {Browser, BrowserContext, CDPSession, chromium, Page} from 'playwright';

// PWA specific constants.
const PWA_MANIFEST_ID = 'https://developer.chrome.com/';

// Color constants for console output.
const COLORS = {
  normal: '\x1b[m',
  green: '\x1b[32m',
  red: '\x1b[31m',
  yellow: '\x1b[33m',
};

// Helper for colorless environments.
if (process.stderr && !process.stderr.hasColors()) {
  (Object.keys(COLORS) as (keyof typeof COLORS)[]).forEach(
      (key) => COLORS[key] = '');
}

/**
 * Helper to send CDP commands with logging.
 */
async function sendCDPCommand(
    session: CDPSession, command: string, params: object): Promise<any> {
  console.log(COLORS.green, `Sending CDP command: ${command}`, COLORS.normal);
  try {
    const result = await session.send(command as any, params as any);
    // Log response, trimmed if too long.
    const resultStr = JSON.stringify(result);
    console.log(`Response: ${
        resultStr.length > 200 ? resultStr.substring(0, 200) + '...' :
                                 resultStr}`);
    return result;
  } catch (e: any) {
    console.error(
        COLORS.red, `CDP Error (${command}): ${e.message}`, COLORS.normal);
    throw e;
  }
}

async function runPWALifecycleTest() {
  const port = process.argv[2] || '9222';
  const browserURL = `http://localhost:${port}`;

  let browser: Browser|null = null;
  let context: BrowserContext|null = null;
  let managementPage: Page|null = null;
  let pwaPage: Page|null = null;

  try {
    console.log(`Attempting to connect to browser at ${browserURL}...`);
    browser = await chromium.connectOverCDP(browserURL);
    console.log('Successfully connected.');

    // Get default context or create one if missing.
    if (browser.contexts().length > 0) {
      context = browser.contexts()[0];
    } else {
      context = await browser.newContext();
    }

    // 1. Create a session to perform PWA management commands.
    // We open a blank page to attach the CDP session to.
    managementPage = await context.newPage();
    const client = await context.newCDPSession(managementPage);
    console.log('Created CDP session for PWA management.');

    const manifestParam = {manifestId: PWA_MANIFEST_ID};
    const installParam = {
      ...manifestParam,
      installUrlOrBundleUrl: PWA_MANIFEST_ID,
    };

    // 2. Install the PWA.
    console.log('Installing PWA from:', installParam.installUrlOrBundleUrl);
    await sendCDPCommand(client, 'PWA.install', installParam);
    console.log('PWA installation command sent.');

    // 3. Launch the PWA.
    console.log('Launching PWA with manifest ID:', manifestParam.manifestId);
    await sendCDPCommand(client, 'PWA.launch', manifestParam);

    // 4. Wait for the PWA window to open.
    console.log('Waiting for PWA window to open... (Timeout: 15 seconds)');

    // Playwright waits for the 'page' event on the context.
    pwaPage = await context.waitForEvent('page', {
      predicate: (page) => page.url().startsWith(PWA_MANIFEST_ID),
      timeout: 15000
    });

    await pwaPage.bringToFront();
    console.log('PWA launched successfully and window is focused.');

    // 5. Inspect the PWA's state and manifest.
    console.log('Inspecting PWA\'s OS state and manifest...');
    await sendCDPCommand(client, 'PWA.getOsAppState', manifestParam);

    // Create a specific CDP session for the PWA page itself to query its
    // specific page manifest.
    const pwaSession = await context.newCDPSession(pwaPage);
    await sendCDPCommand(pwaSession, 'Page.getAppManifest', {});

    // 6. Test opening the current page in its app window.
    console.log('Testing \'openCurrentPageInApp\' command...');
    await sendCDPCommand(pwaSession, 'PWA.openCurrentPageInApp', manifestParam);

    console.log('Test complete. Proceeding to uninstall.');

  } catch (error: any) {
    console.error(
        COLORS.red, 'An error occurred during the test:', error.message,
        COLORS.normal);

    // Screenshot logic.
    if (pwaPage) {
      try {
        const screenshotDir = path.join(__dirname, 'screenshots');
        if (!fs.existsSync(screenshotDir)) {
          fs.mkdirSync(screenshotDir);
        }
        const screenshotPath =
            path.join(screenshotDir, 'failed_pwa_test_ts.png');
        await pwaPage.screenshot({path: screenshotPath});
        console.log(
            COLORS.yellow, `Screenshot saved to ${screenshotPath}`,
            COLORS.normal);
      } catch (screenshotError) {
        console.error('Failed to take screenshot:', screenshotError);
      }
    }
    process.exitCode = 1;
  } finally {
    console.log('Cleanup Phase...');
    // 7. Uninstall PWA and shutdown.
    if (browser && context) {
      // We use a valid page session for uninstallation.
      // If managementPage is closed, create a temporary one.
      let cleanupPage = managementPage;
      if (!cleanupPage || cleanupPage.isClosed()) {
        try {
          cleanupPage = await context.newPage();
        } catch (e) { /* ignore */
        }
      }

      if (cleanupPage && !cleanupPage.isClosed()) {
        console.log('Attempting to uninstall PWA...');
        try {
          const client = await context.newCDPSession(cleanupPage);

          // Close the PWA window first if it's open.
          if (pwaPage && !pwaPage.isClosed()) {
            console.log('Closing PWA window before uninstalling...');
            await pwaPage.close();
          }

          await sendCDPCommand(
              client, 'PWA.uninstall', {manifestId: PWA_MANIFEST_ID});
          console.log('PWA uninstalled.');
        } catch (e: any) {
          console.warn(
              COLORS.yellow,
              'Warning: Failed to uninstall PWA during cleanup:', e.message,
              COLORS.normal);
        }

        // Only close if we created a temp page or it's the original management
        // page.
        if (cleanupPage) await cleanupPage.close();
      }

      console.log('Disconnecting from browser...');
      await browser.close();
      console.log('Shutdown complete.');
    }
  }
}

runPWALifecycleTest();
