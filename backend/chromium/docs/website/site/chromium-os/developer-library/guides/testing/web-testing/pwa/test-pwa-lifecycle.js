// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Puppeteer Test Script for a Standard PWA Lifecycle
 *
 * This script connects to a remote Chrome instance, installs a standard PWA
 * using the `PWA` Chrome DevTools Protocol (CDP) domain. It then launches the
 * PWA, checks its state, and finally uninstalls it.
 *
 * Prerequisites:
 * 1. A CrOS device or VM setup for remote testing as described in the guide:
 *    https://www.chromium.org/chromium-os/developer-library/guides/testing/web-testing/
 * 2. Specifically, Chrome must be launched with both of the following flags:
 *    - `--remote-debugging-port=<port>`
 *    - `--enable-devtools-pwa-handler`
 */

import puppeteer from 'puppeteer-core';
import * as fs from 'node:fs/promises';

// Color constants for console output
const Colors = {
  normal: '\x1b[m',
  green: '\x1b[32m',
  red: '\x1b[31m',
  yellow: '\x1b[33m',
};

// Disable colors if the terminal does not support them
if (!process.stderr.hasColors()) {
  Object.keys(Colors).forEach((key) => Colors[key] = '');
}

// Global variables to hold the browser instance and main session
let browser;
let browserSession;
let pwaPage;

// PWA specific constants
const pwaManifestId = 'https://developer.chrome.com/';

/**
 * Trims a string to a maximum length for cleaner logging.
 * @param {string} text - The text to trim.
 * @returns {string} The original or trimmed text.
 */
function trim(text) {
  const len = 200;
  if (!text || typeof text !== 'string' || text.length < len) {
    return text;
  }
  return text.substring(0, len) + '...';
}

/**
 * Sends a command via the Chrome DevTools Protocol (CDP).
 * @param {import('puppeteer').CDPSession | null} session - The CDP session. If
 * null, uses the main browser session.
 * @param {string} msg - The CDP method name.
 * @param {object} param - The parameters for the CDP method.
 * @returns {Promise<any>} The result of the CDP command.
 */
async function send(session, msg, param) {
  const targetSession = session || browserSession;

  if (!targetSession) {
    console.error(
        Colors.red, 'Error: No valid session to send command.', Colors.normal);
    throw new Error('No valid session for CDP command.');
  }

  console.group(Colors.green, `Sending: ${msg}:`, JSON.stringify(param));
  try {
    const result = await targetSession.send(msg, param);
    console.log(`Response: ${trim(JSON.stringify(result))}`);
    return result;
  } catch (e) {
    console.error(Colors.red, `Error: ${e.message}`, Colors.normal);
    throw e;
  } finally {
    console.groupEnd();
    console.log(Colors.normal);
  }
}

/**
 * Takes a screenshot of the current PWA page if it's available.
 * @param {string} filename - The name of the file to save the screenshot.
 */
async function takeScreenshot(filename = 'pwa_error_screenshot.png') {
  if (pwaPage && !pwaPage.isClosed()) {
    try {
      await fs.mkdir('./screenshots', {recursive: true});
      await pwaPage.screenshot({path: `./screenshots/${filename}`});
      console.log(
          Colors.yellow, `Screenshot saved to ./screenshots/${filename}`,
          Colors.normal);
    } catch (e) {
      console.error(
          Colors.red, 'Failed to take screenshot:', e.message, Colors.normal);
    }
  } else {
    console.warn(
        'Cannot take screenshot: PWA page is not available or already closed.');
  }
}

/**
 * Attempts to uninstall the PWA. This is called during cleanup.
 */
async function uninstallPWA() {
  console.log('Attempting to uninstall PWA with manifestId:', pwaManifestId);
  try {
    // Close the PWA window first if it's still open to ensure a clean
    // uninstall.
    if (pwaPage && !pwaPage.isClosed()) {
      console.log('Closing PWA window before uninstalling...');
      await pwaPage.close();
    }
    await send(null, 'PWA.uninstall', {manifestId: pwaManifestId});
    console.log('PWA uninstallation command sent.');
  } catch (e) {
    console.warn(
        Colors.yellow, 'Warning: Failed to uninstall PWA during cleanup:',
        e.message,
        '. It might not have been installed, or was already uninstalled.',
        Colors.normal);
  }
}

/**
 * Cleans up resources by disconnecting from the browser.
 */
async function shutdown() {
  console.log('Shutting down...');
  if (browser && browser.isConnected()) {
    console.log('Disconnecting from browser.');
    browser.disconnect();
  }
}

(async () => {
  // Get port from command line arguments. Default to 9222 if not provided.
  const port = process.argv[2] || '9222';
  const browserURL = `http://localhost:${port}`;

  try {
    // 1. Connect to the running browser instance.
    console.log(`Attempting to connect to browser at ${browserURL}...`);
    browser = await puppeteer.connect({
      browserURL: browserURL,
      defaultViewport: null,
    });
    console.log('Successfully connected to the browser.');

    browserSession = await browser.target().createCDPSession();
    console.log('Created main browser CDP session.');

    const manifestParam = {manifestId: pwaManifestId};
    const installParam = {
      ...manifestParam,
      installUrlOrBundleUrl: pwaManifestId,
    };

    // 2. Install the PWA.
    console.log('Installing PWA from:', installParam.installUrlOrBundleUrl);
    await send(null, 'PWA.install', installParam);
    console.log('PWA installation command sent.');

    // 3. Launch the PWA.
    console.log('Launching PWA with manifest ID:', manifestParam.manifestId);
    await send(null, 'PWA.launch', manifestParam);

    // 4. Wait for the PWA window to open.
    console.log('Waiting for PWA window to open... (Timeout: 15 seconds)');
    const pwaTarget = await browser.waitForTarget(
        (target) => target.url().startsWith(pwaManifestId) &&
            target.type() === 'page',
        {timeout: 15000});

    if (!pwaTarget) {
      throw new Error('PWA window did not open within the timeout.');
    }
    pwaPage = await pwaTarget.page();
    if (!pwaPage) {
      throw new Error('Could not get page object for the PWA target.');
    }
    await pwaPage.bringToFront();
    console.log('PWA launched successfully and window is focused.');

    // 5. Inspect the PWA's state and manifest.
    console.log("Inspecting PWA's OS state and manifest...");
    await send(null, 'PWA.getOsAppState', manifestParam);
    const pwaPageSession = await pwaPage.createCDPSession();
    await send(pwaPageSession, 'Page.getAppManifest', {});

    // 6. Test opening the current page in its app window.
    console.log("Testing 'openCurrentPageInApp' command...");
    await send(pwaPageSession, 'PWA.openCurrentPageInApp', manifestParam);
    console.log('Test complete. Uninstalling the PWA.');

  } catch (error) {
    console.error(
        Colors.red, 'An error occurred during the test:', error.message,
        Colors.normal);
    await takeScreenshot('failed_pwa_test.png');
  } finally {
    // 7. Uninstall PWA and shutdown.
    await uninstallPWA();
    await shutdown();
  }
})();
