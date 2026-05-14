// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Puppeteer Test Script for Kitchen Sink IWA Interaction
 *
 * This script connects to a remote ChromeOS instance, installs the
 * Kitchen Sink Isolated Web App (IWA) from its web bundle, then
 * launches it, and interacts with its UI to simulate sending and
 * receiving messages via direct sockets.
 *
 * Prerequisites:
 * 1. A CrOS device or VM setup for remote testing as described in the guide:
 *    https://www.chromium.org/chromium-os/developer-library/guides/testing/web-testing/
 * 2. Chrome must be launched with:
 * - `--remote-debugging-port=<port>`
 * - `--enable-devtools-pwa-handler`
 * - `--enable-features=IsolatedWebAppDevMode`
 */

import * as fs from 'node:fs/promises';
import {stdin as input, stdout as output} from 'node:process';
import * as readline from 'node:readline/promises';

import puppeteer from 'puppeteer-core';

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

let browser;
let browserSession;
let browserURL;
let iwaPage;
let iwaFrame;
const rl = readline.createInterface({input, output});

const IWA_OPEN_TIMEOUT_MS = 45000;
const UI_ELEMENT_TIMEOUT_MS = 30000;
const SHADOW_DOM_ELEMENT_TIMEOUT_MS = 15000;
const POLL_INTERVAL_MS = 250;
const TYPING_DELAY_MS = 2000;

// IWA specific constants (moved to global for cleanup access)
const KITCHEN_SINK_IWA_WEB_BUNDLE_ID =
    'aiv4bxauvcu3zvbu6r5yynoh4atkzqqaoeof5mwz54b4zfywcrjuoaacai';
const KITCHEN_SINK_IWA_VERSION = '0.18.0';
const KITCHEN_SINK_IWA_WEB_BUNDLE_URL =
    `https://github.com/chromeos/iwa-sink/releases/download/v` +
    `${KITCHEN_SINK_IWA_VERSION}/iwa-sink.swbn`;

const MESSAGE_FROM_LEFT = 'Sing me a song!';
const MESSAGE_FROM_RIGHT = 'Hello from the right side!';

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
 * @param {import('puppeteer').CDPSession | null} session -
 * The CDP session. If null, uses the main browser session.
 * @param {string} method - The CDP method name.
 * @param {object} params - The parameters for the CDP method.
 * @returns {Promise<any>} The result of the CDP command.
 */
async function send(session, method, params) {
  const targetSession = session || browserSession;
  if (!targetSession) {
    console.error(
        Colors.red, 'Error: No valid session to send command.', Colors.normal);
    throw new Error('No valid session for CDP command.');
  }

  console.group(Colors.green, `Sending: ${method}:`, JSON.stringify(params));

  try {
    const result = await targetSession.send(method, params);
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
 * Takes a screenshot of the current IWA page if it's available.
 * @param {string} filename - The name of the file to save the screenshot.
 */
async function takeScreenshot(filename = 'error_screenshot.png') {
  if (iwaPage && !iwaPage.isClosed()) {
    try {
      await fs.mkdir('./screenshots', {recursive: true});
      await iwaPage.screenshot({path: `./screenshots/${filename}`});
      console.log(
          Colors.yellow, `Screenshot saved to ./screenshots/${filename}`,
          Colors.normal);
    } catch (e) {
      console.error(
          Colors.red, 'Failed to take screenshot:', e.message, Colors.normal);
    }
  } else {
    console.warn(
        'Cannot take screenshot: IWA page is not available or already closed.');
  }
}

/**
 * Cleans up resources by disconnecting from the browser and exiting the script.
 */
async function shutdown() {
  console.log('Shutting down...');
  rl.close();
  if (browser && browser.isConnected()) {
    console.log('Disconnecting from browser.');
    browser.disconnect();
  }
  process.exit(0);
}

/**
 * Attempts to uninstall the IWA. This is called during cleanup.
 */
async function uninstallIWA() {
  console.log(
      'Attempting to uninstall IWA with ID:', KITCHEN_SINK_IWA_WEB_BUNDLE_ID);
  try {
    await send(null, 'PWA.uninstall', {
      manifestId: `isolated-app://${KITCHEN_SINK_IWA_WEB_BUNDLE_ID}/`,
    });
    console.log('IWA uninstallation command sent (or already uninstalled).');
  } catch (e) {
    console.warn(
        Colors.yellow, 'Warning: Failed to uninstall IWA during cleanup:',
        e.message,
        '. It might not have been installed, or was already uninstalled.',
        Colors.normal);
  }
}

(async () => {
  // Get port from command line arguments. Default to 9222 if not provided.
  const port = process.argv[2] || '9222';
  browserURL = `http://localhost:${port}`;
  try {
    console.log(`Attempting to connect to browser at ${browserURL}...`);
    browser = await puppeteer.connect(
        {browserURL: browserURL, defaultViewport: null});
    console.log('Successfully connected to the browser.');

    browserSession = await browser.target().createCDPSession();
    console.log('Created main browser CDP session.');

    console.log('\nInstallation Phase');
    console.log(
        'Now I will attempt to install the Kitchen Sink IWA from its web',
        'bundle.');

    console.log(
        'Attempting to install IWA with ID:', KITCHEN_SINK_IWA_WEB_BUNDLE_ID,
        'from URL:', KITCHEN_SINK_IWA_WEB_BUNDLE_URL);
    await send(null, 'PWA.install', {
      manifestId: `isolated-app://${KITCHEN_SINK_IWA_WEB_BUNDLE_ID}/`,
      installUrlOrBundleUrl: KITCHEN_SINK_IWA_WEB_BUNDLE_URL,
    });
    console.log('IWA installation command sent. Waiting for it to complete...');

    const {targetId: iwaTargetId} = await send(null, 'PWA.launch', {
      manifestId: `isolated-app://${KITCHEN_SINK_IWA_WEB_BUNDLE_ID}/`,
    });
    if (!iwaTargetId) {
      throw new Error('PWA.launch did not return a targetId.');
    }
    console.log(
        'Sent PWA.launch command for IWA:', KITCHEN_SINK_IWA_WEB_BUNDLE_ID,
        `got targetId: ${iwaTargetId}`);

    console.log('\nLaunch & Wait Phase');
    console.log('Waiting for IWA window to open... (Timeout: 45 seconds)');
    const timeout = IWA_OPEN_TIMEOUT_MS;
    const startTime = Date.now();
    let iwaTarget;
    while (Date.now() - startTime < timeout) {
      const targets = await browser.targets();
      iwaTarget = targets.find(
          (target) => target._targetId === iwaTargetId ||
              target.url().startsWith(
                  `isolated-app://${KITCHEN_SINK_IWA_WEB_BUNDLE_ID}/`));
      if (iwaTarget) {
        iwaPage = await iwaTarget.page();
        if (iwaPage) {
          break;
        }
      }
      await new Promise((resolve) => setTimeout(resolve, POLL_INTERVAL_MS));
    }

    if (!iwaPage) {
      throw new Error(
          `IWA window with targetId ${iwaTargetId} did not open or its page ` +
          'was not available within the timeout.');
    }
    await iwaPage.bringToFront();
    console.log(
        'Kitchen Sink IWA launched successfully. Interacting with its UI.');

    iwaFrame = iwaPage.mainFrame();
    console.log(
        'All UI interactions will be targeted to the main frame of the IWA',
        'window.');

    console.log('\nUI Interaction Phase');
    const CREATE_SOCKET_CONN_BUTTON_SELECTOR = '#addSocketButton';

    console.log(
        "Waiting for 'Create new socket connection' button...",
        '(Timeout: 30 seconds)');
    await iwaFrame.waitForSelector(
        CREATE_SOCKET_CONN_BUTTON_SELECTOR,
        {timeout: UI_ELEMENT_TIMEOUT_MS});
    console.log("Found 'Create new socket connection' button.");

    await iwaFrame.click(CREATE_SOCKET_CONN_BUTTON_SELECTOR);
    console.log("Clicked 'Create new socket connection' button.");

    // Get references to the custom components
    const leftSocketComponent = await iwaFrame.waitForSelector(
        'socket-connection', {timeout: SHADOW_DOM_ELEMENT_TIMEOUT_MS});
    if (!leftSocketComponent) {
      throw new Error('Left socket-connection component not found.');
    }

    const rightSocketComponent = await iwaFrame.waitForSelector(
        'socket-server', {timeout: SHADOW_DOM_ELEMENT_TIMEOUT_MS});
    if (!rightSocketComponent) {
      throw new Error('Right socket-server component not found.');
    }

    // Get references to their shadow roots
    const leftSocketShadowRootHandle =
        await leftSocketComponent.evaluateHandle((el) => el.shadowRoot);
    const rightSocketShadowRootHandle =
        await rightSocketComponent.evaluateHandle((el) => el.shadowRoot);

    if (!leftSocketShadowRootHandle ||
        await leftSocketShadowRootHandle.evaluate((s) => s === null)) {
      throw new Error(
          'Left socket-connection component does not have an accessible ' +
          "shadow root or it's null.");
    }
    if (!rightSocketShadowRootHandle ||
        await rightSocketShadowRootHandle.evaluate((s) => s === null)) {
      throw new Error(
          'Right socket-server component does not have an accessible ' +
          "shadow root or it's null.");
    }
    console.log(
        'Accessed shadow roots of socket-connection and socket-server',
        'components.');

    // Define selectors *within* the shadow DOMs (relative to the shadow root)
    const INPUT_SHADOW_SELECTOR = 'input#messageInput';
    const SEND_BUTTON_SHADOW_SELECTOR = 'button#sendButton';

    const searchInShadowRootForText =
        async (shadowRootHandle, messageText) => {
      return await iwaFrame.waitForFunction(
          (shadowRoot, text) => {
            const search = (el) => {
              if (el.textContent && el.textContent.includes(text)) {
                return true;
              }
              if (el.shadowRoot) {
                return search(el.shadowRoot);
              }
              return false;
            };
            if (search(shadowRoot)) return true;
            for (const el of Array.from(shadowRoot.querySelectorAll('*'))) {
              if (search(el)) return true;
            }
            return false;
          },
          {timeout: SHADOW_DOM_ELEMENT_TIMEOUT_MS}, shadowRootHandle,
          messageText);
    };

    // 1. Send message from LEFT side to RIGHT side
    console.log('\nMessage Test: Left -> Right');
    console.log(
        "Waiting for left 'Send a message' text field and button within",
        'shadow DOM...');

    const leftInputField = await leftSocketShadowRootHandle.waitForSelector(
        INPUT_SHADOW_SELECTOR, {timeout: SHADOW_DOM_ELEMENT_TIMEOUT_MS});
    if (!leftInputField) {
      throw new Error('Left input field not found in shadow DOM.');
    }

    const leftSendButton = await leftSocketShadowRootHandle.waitForSelector(
        SEND_BUTTON_SHADOW_SELECTOR,
        {timeout: SHADOW_DOM_ELEMENT_TIMEOUT_MS});
    if (!leftSendButton) {
      throw new Error('Left send button not found in shadow DOM.');
    }

    console.log(`Typing '${MESSAGE_FROM_LEFT}' into the left text field...`);
    await leftInputField.type(MESSAGE_FROM_LEFT);
    await new Promise((resolve) => setTimeout(resolve, TYPING_DELAY_MS));
    await leftSendButton.click();
    console.log(`Typed '${MESSAGE_FROM_LEFT}' and clicked Send on the left.`);

    console.log(
        `Waiting for '${MESSAGE_FROM_LEFT}' message to appear on the right`,
        'side...');
    await searchInShadowRootForText(
        rightSocketShadowRootHandle, MESSAGE_FROM_LEFT);
    console.log(
        `Verified '${MESSAGE_FROM_LEFT}' message appeared on the right side.`);

    // 2. Send message from RIGHT side to LEFT side
    console.log('\nMessage Test: Right -> Left');
    console.log(
        "Waiting for right 'Send a message' text field and button within",
        'shadow DOM...');

    const rightInputField = await rightSocketShadowRootHandle.waitForSelector(
        INPUT_SHADOW_SELECTOR, {timeout: SHADOW_DOM_ELEMENT_TIMEOUT_MS});
    if (!rightInputField) {
      throw new Error('Right input field not found in shadow DOM.');
    }

    const rightSendButton = await rightSocketShadowRootHandle.waitForSelector(
        SEND_BUTTON_SHADOW_SELECTOR,
        {timeout: SHADOW_DOM_ELEMENT_TIMEOUT_MS});
    if (!rightSendButton) {
      throw new Error('Right send button not found in shadow DOM.');
    }

    console.log(`Typing '${MESSAGE_FROM_RIGHT}' into the right text field...`);
    await rightInputField.type(MESSAGE_FROM_RIGHT);
    await new Promise((resolve) => setTimeout(resolve, TYPING_DELAY_MS));
    await rightSendButton.click();
    console.log(
        `Typed '${MESSAGE_FROM_RIGHT}' and clicked Send on the right.`);

    console.log(
        `Waiting for '${MESSAGE_FROM_RIGHT}' message to appear on the left`,
        'side...');
    await searchInShadowRootForText(
        leftSocketShadowRootHandle, MESSAGE_FROM_RIGHT);
    console.log(
        `Verified '${MESSAGE_FROM_RIGHT}' message appeared on the left side.`);

    // Close the IWA window.
    if (iwaPage && !iwaPage.isClosed()) {
      await iwaPage.close();
      console.log('Closed Kitchen Sink IWA window.');
    } else {
      console.log('IWA page was already closed or not found.');
    }

    // Explicit Uninstall at the end of successful path
    console.log('\nCleanup Phase');
    await uninstallIWA();
    console.log('IWA uninstallation process initiated.');
  } catch (error) {
    console.error(
        Colors.red, '--- An error occurred during the test ---', Colors.normal);
    console.error('Error details:', error.message);
    await takeScreenshot('failed_iwa_test.png');
    console.log(
        'Please check the console output and the screenshot for more',
        'details.');
    // Cleanup on error: Attempt to uninstall IWA
    console.log('Attempting cleanup (uninstall IWA) due to test failure...');
    await uninstallIWA();
  } finally {
    await shutdown();
  }
})();
