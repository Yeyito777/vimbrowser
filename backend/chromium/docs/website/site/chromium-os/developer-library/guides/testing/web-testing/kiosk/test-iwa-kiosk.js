// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Puppeteer Test Script for an IWA in Kiosk Mode
 *
 * This script connects to a remote ChromeOS instance running in Kiosk mode,
 * finds the target for the launched Isolated Web App (IWA), and interacts
 * with its UI using the Chrome DevTools Protocol (CDP).
 *
 * This script uses lower-level CDP commands via `puppeteer-core` because IWAs
 * in kiosk mode run as a special target type ('other'), not a standard 'page',
 * which limits the use of some Puppeteer Page APIs.
 *
 * Prerequisites:
 * 1. A CrOS device or VM setup for remote testing and configured for Kiosk mode
 *    as described in the guide:
 *    https://www.chromium.org/chromium-os/developer-library/guides/testing/web-testing/
 * 2. The device must have policies set to auto-launch the IWA in Kiosk mode.
 * 3. Chrome must be launched with **all** of the following flags:
 * - `--remote-debugging-port=<port>`
 * - `--remote-debugging-address=0.0.0.0`
 * - `--force-devtools-available`
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

const IWA_WEB_BUNDLE_ID =
    'aiv4bxauvcu3zvbu6r5yynoh4atkzqqaoeof5mwz54b4zfywcrjuoaacai';

// Global variables for cleanup
let browser;
let session;

/**
 * Takes a screenshot using the Page domain if enabled, or warns if not possible
 * Note: In Kiosk 'other' targets, Page domain might not be fully supported,
 * but this attempts to capture state on failure.
 * @param {string} filename - The name of the file to save the screenshot.
 */
async function takeScreenshot(filename = 'kiosk_error_screenshot.png') {
  if (session) {
    try {
      // Ensure Page domain is enabled for screenshotting
      await session.send('Page.enable');
      const {data} = await session.send('Page.captureScreenshot');
      await fs.mkdir('./screenshots', {recursive: true});
      await fs.writeFile(`./screenshots/${filename}`, data, 'base64');
      console.log(
          Colors.yellow, `Screenshot saved to ./screenshots/${filename}`,
          Colors.normal);
    } catch (e) {
      console.error(
          Colors.red, 'Failed to take screenshot:', e.message, Colors.normal);
    }
  } else {
    console.warn('Cannot take screenshot: No active CDP session available.');
  }
}

/**
 * Clicks an element using CDP commands.
 * @param {import('puppeteer').CDPSession} session - The CDP session.
 * @param {number} nodeId - The nodeId of the element to click.
 */
async function clickElement(session, nodeId) {
  const {model} = await session.send('DOM.getBoxModel', {nodeId});
  const {content} = model;
  const x = (content[0] + content[2]) / 2;
  const y = (content[1] + content[5]) / 2;

  await session.send('Input.dispatchMouseEvent', {
    type: 'mousePressed',
    button: 'left',
    x,
    y,
    clickCount: 1,
  });
  await session.send('Input.dispatchMouseEvent', {
    type: 'mouseReleased',
    button: 'left',
    x,
    y,
    clickCount: 1,
  });
}

/**
 * Types text into an element using CDP commands.
 * @param {import('puppeteer').CDPSession} session - The CDP session.
 * @param {number} nodeId - The nodeId of the input element.
 * @param {string} text - The text to type.
 */
async function typeText(session, nodeId, text) {
  await session.send('DOM.focus', {nodeId});
  for (const char of text) {
    await session.send(
        'Input.dispatchKeyEvent', {type: 'char', text: char});
  }
}

/**
 * Polls the DOM using Runtime.evaluate to check for a condition.
 * @param {import('puppeteer').CDPSession} session - The CDP session.
 * @param {Function} pageFunction - The function to execute in the browser
 * context.
 * @param {number} timeout - The max time to wait.
 * @param {...any} args - Arguments to pass to the pageFunction.
 * @returns {Promise<void>}
 */
async function waitForCondition(session, pageFunction, timeout, ...args) {
  const startTime = Date.now();
  while (Date.now() - startTime < timeout) {
    const expression = `(${pageFunction.toString()})(${args.map(
        JSON.stringify).join(',')})`;

    const {result} = await session.send('Runtime.evaluate', {
      expression: expression,
      returnByValue: true,
    });
    if (result.value) return;
    await new Promise((resolve) => setTimeout(resolve, 500));
  }
  throw new Error(`waitForCondition timed out after ${timeout}ms`);
}

(async () => {
  // Get port from command line arguments. Default to 9222 if not provided.
  const port = process.argv[2] || '9222';
  const browserURL = `http://localhost:${port}`;
  const iwaUrlPattern = `isolated-app://${IWA_WEB_BUNDLE_ID}`;

  try {
    // 1. Connect to the browser
    console.log(`Attempting to connect to browser at ${browserURL}...`);
    browser = await puppeteer.connect({browserURL, defaultViewport: null});
    console.log('Successfully connected to the browser on the DUT.');

    // 2. Wait for the IWA target and create a CDP session
    const iwaTarget = await browser.waitForTarget(
        (target) => target.url().startsWith(iwaUrlPattern) &&
            target.type() === 'other');
    console.log(`Found IWA target: ${iwaTarget.url()}`);

    session = await iwaTarget.createCDPSession();
    console.log('CDP session created.');

    // 3. Enable necessary CDP domains
    await session.send('Runtime.enable');
    await session.send('DOM.enable');
    console.log('Enabled Runtime and DOM domains.');

    // 4. Get the root of the document
    const {root} = await session.send('DOM.getDocument');

    // Click "Create new socket connection" button
    console.log("Waiting for 'Create new socket connection' button...");
    const {nodeId: addSocketButtonNodeId} =
        await session.send('DOM.querySelector', {
          nodeId: root.nodeId,
          selector: '#addSocketButton',
        });
    if (!addSocketButtonNodeId) {
      throw new Error('Could not find #addSocketButton');
    }
    await clickElement(session, addSocketButtonNodeId);
    console.log("Clicked 'Create new socket connection' button.");

    console.log('Waiting for socket components to appear...');

    // Get left and right components
    const {nodeId: leftCompNodeId} = await session.send('DOM.querySelector', {
      nodeId: root.nodeId,
      selector: 'socket-connection',
    });
    if (!leftCompNodeId) {
      throw new Error('Could not find socket-connection component.');
    }
    const {nodeId: rightCompNodeId} = await session.send('DOM.querySelector', {
      nodeId: root.nodeId,
      selector: 'socket-server',
    });
    if (!rightCompNodeId) {
      throw new Error('Could not find socket-server component.');
    }
    // Get their shadow roots
    const {node: leftCompNode} =
        await session.send('DOM.describeNode', {nodeId: leftCompNodeId});
    const leftShadowRootNodeId = leftCompNode.shadowRoots[0].nodeId;

    const {node: rightCompNode} =
        await session.send('DOM.describeNode', {nodeId: rightCompNodeId});
    const rightShadowRootNodeId = rightCompNode.shadowRoots[0].nodeId;
    console.log('Found shadow roots for both components.');

    // Define selectors within the shadow DOM
    const inputShadowSelector = 'input#messageInput';
    const sendButtonShadowSelector = 'button#sendButton';

    // 1. Send message from LEFT to RIGHT
    console.log('Interacting with LEFT component...');
    const {nodeId: leftInputId} = await session.send('DOM.querySelector', {
      nodeId: leftShadowRootNodeId,
      selector: inputShadowSelector,
    });
    const {nodeId: leftSendBtnId} = await session.send('DOM.querySelector', {
      nodeId: leftShadowRootNodeId,
      selector: sendButtonShadowSelector,
    });

    await typeText(session, leftInputId, 'Sing me a song!');
    await clickElement(session, leftSendBtnId);
    console.log("Typed 'Sing me a song!' and clicked Send on the left.");

    console.log('Waiting for message to appear on the right side...');
    await waitForCondition(
        session,
        (selector, text) => {
          const root = document.querySelector(selector);
          return root && root.shadowRoot &&
              root.shadowRoot.innerHTML.includes(text);
        },
        15000, 'socket-server', 'Sing me a song!');
    console.log(
        "Verified 'Sing me a song!' message appeared on the right side.");

    // 2. Send message from RIGHT to LEFT
    console.log('Interacting with RIGHT component...');
    const {nodeId: rightInputId} = await session.send('DOM.querySelector', {
      nodeId: rightShadowRootNodeId,
      selector: inputShadowSelector,
    });
    const {nodeId: rightSendBtnId} = await session.send('DOM.querySelector', {
      nodeId: rightShadowRootNodeId,
      selector: sendButtonShadowSelector,
    });

    await typeText(session, rightInputId, 'Hello from the right side!');
    await clickElement(session, rightSendBtnId);
    console.log(
        "Typed 'Hello from the right side!' and clicked Send on the right.");

    console.log('Waiting for message to appear on the left side...');
    await waitForCondition(
        session,
        (selector, text) => {
          const root = document.querySelector(selector);
          return root && root.shadowRoot &&
              root.shadowRoot.innerHTML.includes(text);
        },
        15000, 'socket-connection', 'Hello from the right side!');
    console.log(
        "Verified 'Hello from the right side!' message appeared on the",
        'left side.');

    console.log('\nTest finished successfully!');

  } catch (err) {
    console.error(
        Colors.red, 'An error occurred:', err.message, Colors.normal);
    await takeScreenshot('failed_kiosk_iwa_test.png');
  } finally {
    if (browser) {
      console.log('Disconnecting from browser.');
      await browser.disconnect();
    }
  }
})();
