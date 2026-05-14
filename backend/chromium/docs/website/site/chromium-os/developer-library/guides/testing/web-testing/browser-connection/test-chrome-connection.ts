// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Playwright Test Script for Verifying Connection and
 * Configuration.
 *
 * This script connects to a remote CrOS instance and navigates to
 * 'chrome://version' to verify that the remote debugging connection is
 * successful and that Chrome has been launched with the necessary flags.
 *
 * Prerequisites:
 * 1. A CrOS device or VM setup for remote testing as described in the guide:
 * /chromium-os/developer-library/guides/testing/web-testing/
 * 2. Chrome must be launched with: `--remote-debugging-port=<port>`
 */

import {Browser, BrowserContext, chromium, Page} from 'playwright';

// Color constants for console output.
const COLORS = {
  normal: '\x1b[m',
  green: '\x1b[32m',
  red: '\x1b[31m',
  yellow: '\x1b[33m',
};

// Disable colors if the terminal does not support them.
if (!process.stderr.hasColors()) {
  (Object.keys(COLORS) as (keyof typeof COLORS)[]).forEach((key) => {
    COLORS[key] = '';
  });
}

async function runTest() {
  const port = process.argv[2] || '9222';
  const browserURL = `http://localhost:${port}`;
  const targetURL = 'chrome://version';
  let browser: Browser|null = null;
  let page: Page|null = null;
  let context: BrowserContext|null = null;

  try {
    // 1. Connect to the existing browser instance via CDP.
    console.log(`Attempting to connect to browser at ${browserURL}...`);
    browser = await chromium.connectOverCDP(browserURL);
    console.log(
        COLORS.green, 'Successfully connected to the browser.', COLORS.normal);

    // 2. Get the default context or create one if missing.
    // When connecting over CDP, contexts usually represent existing profiles.
    if (browser.contexts().length > 0) {
      context = browser.contexts()[0];
    } else {
      console.log('No existing context found, creating a new one...');
      context = await browser.newContext();
    }

    if (!context) {
      throw new Error('Could not obtain a browser context.');
    }

    page = await context.newPage();

    console.log(`Navigating to ${targetURL}...`);

    // FIX: chrome:// URLs are internal and often do not trigger 'networkidle'.
    // We use 'domcontentloaded' or 'load' to avoid timeout errors.
    await page.goto(targetURL, {waitUntil: 'domcontentloaded'});

    console.log('Successfully navigated and loaded the page.');

    // 3. Extract and log the full content.
    console.log('Extracting content from chrome://version...');

    // Depending on the Chrome version, internal pages might need a slight delay
    // or specific selector if the body isn't immediately interactive.
    try {
      await page.waitForSelector('#version-scan-id', {timeout: 2000})
          .catch(() => {
            // Fallback: If specific ID doesn't exist, just wait a tick for
            // body.
            return page!.waitForSelector('body');
          });
    } catch (e) {
      console.warn(
          'Warning: Could not wait for specific chrome://version elements.');
    }

    const versionContent = await page.innerText('body');

    console.log(
        COLORS.green, '--- Chrome Version Information ---', COLORS.normal);
    console.log(versionContent);

    console.log(
        COLORS.green, 'Test complete. Proceeding with cleanup.', COLORS.normal);
  } catch (error: any) {
    console.error(
        COLORS.red, '--- An error occurred during the test ---', COLORS.normal);
    console.error(error);
    if (error.message && error.message.includes('ECONNREFUSED')) {
      console.error(
          COLORS.yellow,
          'Connection refused. Is the SSH tunnel active and Chrome running',
          'with the correct debugging port?', COLORS.normal);
    }
    console.log('Attempting to shut down gracefully despite the error...');
    // Re-throw to ensure the test runner sees this as a failure.
    process.exitCode = 1;
  } finally {
    console.log('Shutting down...');
    if (page) {
      try {
        await page.close();
      } catch (e) { /* ignore */
      }
    }
    if (browser) {
      try {
        await browser.close();
      } catch (e) { /* ignore */
      }
    }
    console.log('Shutdown complete.');
  }
}

runTest();
