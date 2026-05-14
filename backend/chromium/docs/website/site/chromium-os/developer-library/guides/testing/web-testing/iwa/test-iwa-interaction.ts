// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Playwright Test Script for Kitchen Sink IWA Interaction
 * (TypeScript).
 *
 * This script connects to a remote ChromeOS instance, installs the
 * Kitchen Sink Isolated Web App (IWA) from its web bundle, then
 * launches it, and interacts with its UI to simulate sending and
 * receiving messages via direct sockets.
 *
 * Prerequisites:
 * 1. A CrOS device or VM setup for remote testing.
 * 2. Chrome must be launched with:
 * - `--remote-debugging-port=<port>`
 * - `--enable-devtools-pwa-handler`
 * - `--enable-features=IsolatedWebAppDevMode`
 */

import * as fs from 'fs';
import * as path from 'path';
import {Browser, BrowserContext, CDPSession, chromium, Page} from 'playwright';

// Constants
const KITCHEN_SINK_IWA_WEB_BUNDLE_ID =
    'aiv4bxauvcu3zvbu6r5yynoh4atkzqqaoeof5mwz54b4zfywcrjuoaacai';
const IWA_MANIFEST_ID = `isolated-app://${KITCHEN_SINK_IWA_WEB_BUNDLE_ID}/`;
const KITCHEN_SINK_IWA_VERSION = '0.18.0';
const KITCHEN_SINK_IWA_WEB_BUNDLE_URL =
    `https://github.com/chromeos/iwa-sink/releases/download/v${
        KITCHEN_SINK_IWA_VERSION}/iwa-sink.swbn`;

const MESSAGE_FROM_LEFT = 'Sing me a song!';
const MESSAGE_FROM_RIGHT = 'Hello from the right side!';

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
    return await session.send(command as any, params as any);
  } catch (e: any) {
    console.error(
        COLORS.red, `CDP Error (${command}): ${e.message}`, COLORS.normal);
    throw e;
  }
}

async function runIWAInteractionTest() {
  const port = process.argv[2] || '9222';
  const browserURL = `http://localhost:${port}`;

  let browser: Browser|null = null;
  let context: BrowserContext|null = null;
  let managementPage: Page|null = null;
  let iwaPage: Page|null = null;

  try {
    console.log(`Connecting to browser at ${browserURL}...`);
    browser = await chromium.connectOverCDP(browserURL);
    console.log('Successfully connected.');

    // Get default context or create one.
    if (browser.contexts().length > 0) {
      context = browser.contexts()[0];
    } else {
      context = await browser.newContext();
    }

    // Create a session on a management page to perform PWA commands.
    managementPage = await context.newPage();
    const client = await context.newCDPSession(managementPage);

    // 1. Install IWA.
    console.log('Installing IWA...');
    await sendCDPCommand(client, 'PWA.install', {
      manifestId: IWA_MANIFEST_ID,
      installUrlOrBundleUrl: KITCHEN_SINK_IWA_WEB_BUNDLE_URL,
    });

    // 2. Launch IWA.
    console.log('Launching IWA...');
    await sendCDPCommand(client, 'PWA.launch', {manifestId: IWA_MANIFEST_ID});

    // 3. Wait for IWA Window.
    console.log('Waiting for IWA window to appear...');
    iwaPage = await context.waitForEvent('page', {
      predicate: (page) => page.url().startsWith('isolated-app://'),
      timeout: 15000
    });

    await iwaPage.bringToFront();
    console.log('IWA Launched. Title:', await iwaPage.title());

    // 4. UI Interaction.
    console.log('Interacting with IWA UI...');

    // Click 'Create new socket connection'.
    await iwaPage.click('#addSocketButton');

    // Define Locators for Shadow DOM components.
    // Playwright Locators pierce Shadow DOM boundaries automatically.
    const leftSocket = iwaPage.locator('socket-connection');
    const rightSocket = iwaPage.locator('socket-server');

    // Wait for components to be ready.
    await leftSocket.waitFor();
    await rightSocket.waitFor();
    console.log('Socket components found.');

    // --- TEST 1: Left -> Right ---
    console.log(
        `\n--- Test 1: Sending "${MESSAGE_FROM_LEFT}" from Left to Right ---`);

    const leftInput = leftSocket.locator('input#messageInput');
    const leftSendBtn = leftSocket.locator('button#sendButton');

    await leftInput.fill(MESSAGE_FROM_LEFT);
    await leftSendBtn.click();

    // Verify on Right Side.
    // Use strict selector 'p#log' to avoid ambiguity with <socket-log>.
    const rightLog =
        rightSocket.locator('p#log', {hasText: MESSAGE_FROM_LEFT});
    await rightLog.waitFor({timeout: 10000});
    console.log(
        COLORS.green, 'Verified message received on Right side.',
        COLORS.normal);

    // --- TEST 2: Right -> Left ---
    console.log(
        `\n--- Test 2: Sending "${MESSAGE_FROM_RIGHT}" from Right to Left ---`);

    const rightInput = rightSocket.locator('input#messageInput');
    const rightSendBtn = rightSocket.locator('button#sendButton');

    await rightInput.fill(MESSAGE_FROM_RIGHT);
    await rightSendBtn.click();

    // Verify on Left Side.
    const leftLog = leftSocket.locator('p#log', {hasText: MESSAGE_FROM_RIGHT});
    await leftLog.waitFor({timeout: 10000});
    console.log(
        COLORS.green, 'Verified message received on Left side.', COLORS.normal);

    console.log('IWA interaction test passed.');

  } catch (error: any) {
    console.error(
        COLORS.red, '--- An error occurred during the test ---', COLORS.normal);
    console.error(error);

    // Screenshot on failure.
    if (iwaPage) {
      try {
        const screenshotDir = path.join(__dirname, 'screenshots');
        if (!fs.existsSync(screenshotDir)) fs.mkdirSync(screenshotDir);
        const screenshotPath =
            path.join(screenshotDir, 'failed_iwa_test_ts.png');
        await iwaPage.screenshot({path: screenshotPath});
        console.log(
            COLORS.yellow, `Screenshot saved to ${screenshotPath}`,
            COLORS.normal);
      } catch (e) {
        console.error('Failed to take screenshot:', e);
      }
    }
    process.exitCode = 1;
  } finally {
    console.log('\nCleanup Phase...');
    if (browser && context) {
      try {
        // Need a valid page session to uninstall. Ensure managementPage is open
        // or create new one.
        let cleanupSessionPage = managementPage;
        if (!cleanupSessionPage || cleanupSessionPage.isClosed()) {
          cleanupSessionPage = await context.newPage();
        }
        const client = await context.newCDPSession(cleanupSessionPage);

        console.log(`Uninstalling IWA: ${IWA_MANIFEST_ID}`);
        await sendCDPCommand(
            client, 'PWA.uninstall', {manifestId: IWA_MANIFEST_ID});
        console.log('IWA uninstalled.');

        if (cleanupSessionPage !== managementPage) {
          await cleanupSessionPage.close();
        }
      } catch (e: any) {
        console.warn(
            COLORS.yellow,
            'Warning: Failed to uninstall IWA during cleanup:', e.message,
            COLORS.normal);
      }

      if (managementPage && !managementPage.isClosed()) {
        await managementPage.close();
      }
      await browser.close();
    }
    console.log('Test finished.');
  }
}

runIWAInteractionTest();
