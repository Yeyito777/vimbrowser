// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Puppeteer Test Script for Verifying Connection and Configuration
 *
 * This script connects to a remote CrOS instance and navigates to
 * 'chrome://version' to verify that the remote debugging connection is
 * successful and that Chrome has been launched with the necessary flags.
 *
 * Prerequisites:
 * 1. A CrOS device or VM setup for remote testing as described in the guide:
 *    https://www.chromium.org/chromium-os/developer-library/guides/testing/web-testing/
 * 2. Chrome must be launched with: `--remote-debugging-port=<port>`
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

// Global variables for resources
let browser = null;
let page = null;

async function takeScreenshot(filename = 'error_screenshot.png') {
  if (page && !page.isClosed()) {
    try {
      await fs.mkdir('./screenshots', {recursive: true});
      await page.screenshot({path: `./screenshots/${filename}`});
      console.log(
          Colors.yellow, `Screenshot saved to ./screenshots/${filename}`,
          Colors.normal);
    } catch (e) {
      console.error(
          Colors.red, 'Failed to take screenshot:', e.message, Colors.normal);
    }
  } else {
    console.warn(
        'Cannot take screenshot: Page is not available or already closed.');
  }
}

async function shutdown() {
  console.log('Shutting down...');
  if (page && !page.isClosed()) {
    console.log('Closing the current page.');
    await page.close();
  }
  if (browser && browser.isConnected()) {
    console.log('Disconnecting from the browser.');
    await browser.disconnect();
  }
  console.log('Shutdown complete.');
}

(async () => {
  // Get port from command line arguments. Default to 9222 if not provided.
  const port = process.argv[2] || '9222';
  const browserURL = `http://localhost:${port}`;
  const TARGET_URL = 'chrome://version';

  try {
    // 1. Connect to the existing browser instance
    console.log(`Attempting to connect to browser at ${browserURL}...`);
    browser = await puppeteer.connect({
      browserURL: browserURL,
      defaultViewport: null,
    });
    console.log('Successfully connected to the browser.');

    // 2. Open a new page and navigate to chrome://version
    console.log(`Navigating to ${TARGET_URL}...`);
    page = await browser.newPage();
    await page.goto(TARGET_URL, {waitUntil: 'networkidle0'});
    console.log('Successfully navigated and loaded the page.');

    // 3. Extract and log the full content
    console.log('Extracting content from chrome://version...');
    const versionContent = await page.evaluate(() => document.body.innerText);

    console.log('Chrome Version Information:');
    console.log(versionContent);

    console.log('Test complete. Proceeding with cleanup.');
  } catch (error) {
    console.error(
        Colors.red, '--- An error occurred during the test ---', Colors.normal);
    console.error(error);
    await takeScreenshot('failed_verify_connection_test.png');
    if (error.message.includes('ECONNREFUSED')) {
      console.error(
          Colors.red,
          'Connection refused. Is the SSH tunnel active and Chrome running',
          'with the correct debugging port?',
          Colors.normal);
    }
    console.log('Attempting to shut down gracefully despite the error...');
  } finally {
    // 4. Ensure cleanup is always performed
    await shutdown();
  }
})();
