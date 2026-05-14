// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * Puppeteer test script for end-to-end file download and verification on
 * ChromeOS. This script uses a robust UI automation approach to control the
 * Files app and the Media App (Backlight).
 *
 * Prerequisites:
 * 1. A CrOS device or VM setup for remote testing as described in the guide:
 *    https://www.chromium.org/chromium-os/developer-library/guides/testing/puppeteer-system-apps/
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

// Global variables for resources
let browser = null;
let browserSession = null;

// Test Configuration
const FILE_CONTENT = 'Hello, ChromeOS E2E Testing!';
const FILE_NAME = 'test-download.txt';
// Split long URL construction to fit 80 chars
const ENCODED_CONTENT = encodeURIComponent(FILE_CONTENT);
const DOWNLOAD_PAGE_URL =
    `data:text/html,<a id="downloadLink" ` +
    `href="data:text/plain;charset=utf-8,${ENCODED_CONTENT}" ` +
    `download="${FILE_NAME}">Download Test File</a>`;
const FILES_APP_URL = 'chrome://file-manager';
const UI_INTERACTION_TIMEOUT = 20000;

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
 */
async function send(session, msg, param) {
  const targetSession = session || browserSession;
  if (!targetSession) {
    console.error(
        Colors.red, 'Error: No valid session to send command.', Colors.normal);
    throw new Error('No valid session for CDP command.');
  }

  // Use grouping for CDP traffic logging
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
 * Takes a screenshot of a given page.
 */
async function takeScreenshot(page, filename = 'error_screenshot.png') {
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
  }
}

/**
 * Browser-side function to verify content in Media App/CodeMirror.
 * This is stringified and sent to the browser.
 * @param {number} timeoutMs - Timeout in milliseconds.
 * @return {Promise<string>} - The found content.
 */
function verifyContentInBrowser(timeoutMs) {
  return new Promise((resolve, reject) => {
    const startTime = Date.now();

    function poll() {
      try {
        // Strategy 1: CodeMirror 6 API
        const editorDom = document.querySelector('.cm-editor');
        if (editorDom && editorDom.cmView && editorDom.cmView.view) {
          const text = editorDom.cmView.view.state.doc.toString();
          if (text && text.trim().length > 0) {
            resolve(text);
            return;
          }
        }

        // Strategy 2: DOM Text (Fallback)
        const contentNode = document.querySelector('.cm-content');
        if (contentNode) {
          const text = contentNode.innerText;
          if (text && text.trim().length > 0) {
            resolve(text);
            return;
          }
        }
      } catch (e) {
        /* Ignore polling errors */
      }

      if (Date.now() - startTime > timeoutMs) {
        reject(new Error(
            'Timed out. Could not find text in .cm-editor or .cm-content'));
        return;
      }

      setTimeout(poll, 500);
    }
    poll();
  });
}

/**
 * Main function to run the test.
 */
(async () => {
  const port = process.argv[2] || '9222';
  const browserURL = `http://localhost:${port}`;
  let filesAppPage = null;
  let textAppSession = null;
  const fileSelector = `li.table-row[file-name="${FILE_NAME}"]`;

  try {
    // 1. Connect to the browser
    console.log(`Connecting to browser at ${browserURL}...`);
    browser = await puppeteer.connect({
      browserURL: browserURL,
      // Increase viewport to ensure Files App toolbar buttons don't hide
      defaultViewport: {width: 1280, height: 800},
    });
    console.log('Successfully connected.');
    browserSession = await browser.target().createCDPSession();
    console.log('Created main browser CDP session.');

    // Step 2: Download the file
    console.log('Navigating to test page to download file...');
    const downloadPage = await browser.newPage();
    await downloadPage.goto(DOWNLOAD_PAGE_URL);
    await downloadPage.click('#downloadLink');
    console.log(`Clicked download link for "${FILE_NAME}".`);
    await new Promise((resolve) => setTimeout(resolve, 2000));
    await downloadPage.close();

    // Step 3: Launch Files App and navigate to Downloads
    console.log('Launching Files app...');
    await send(null, 'PWA.launch', {manifestId: FILES_APP_URL});
    const filesAppTarget = await browser.waitForTarget(
        (target) => target.url().startsWith(FILES_APP_URL) &&
            target.type() === 'page');
    filesAppPage = await filesAppTarget.page();
    if (!filesAppPage) {
      throw new Error('Failed to get page for Files app.');
    }

    // Ensure the window is large enough
    await filesAppPage.setViewport({width: 1280, height: 800});
    await filesAppPage.bringToFront();
    console.log('Files app launched. Finding "Downloads" folder...');

    const downloadsFolderHandle = await filesAppPage.waitForFunction(
        () => {
          const tree = document.querySelector('#directory-tree');
          if (!tree) return null;
          const items = tree.querySelectorAll('xf-tree-item');
          for (const item of items) {
            // Access shadowRoot to find the label
            const label = item.shadowRoot?.querySelector('span#tree-label');
            if (label?.textContent.trim() === 'Downloads') {
              return item;
            }
          }
        },
        {timeout: UI_INTERACTION_TIMEOUT});

    if (!downloadsFolderHandle) {
      throw new Error('Could not find "Downloads" folder.');
    }

    await downloadsFolderHandle.click();
    console.log('Clicked "Downloads" folder.');

    await filesAppPage.waitForSelector(
        fileSelector, {timeout: UI_INTERACTION_TIMEOUT});
    console.log(`Found "${FILE_NAME}" in Files app.`);

    // Step 4: Open the file and verify content
    console.log('Double-clicking file to open Text/Media app...');
    const TEXT_APP_UI_URL =
        'chrome-extension://mmfbcljfglbokpmkimbfghdkjmjhdgbg/index.html';
    const uiTargetPromise = browser.waitForTarget(
        (target) =>
            target.url() === TEXT_APP_UI_URL && target.type() === 'other',
        {timeout: 30000});

    await filesAppPage.click(fileSelector, {clickCount: 2});

    const uiTarget = await uiTargetPromise;
    if (!uiTarget) {
      throw new Error('Could not find the Text/Media app UI window target.');
    }
    textAppSession = await uiTarget.createCDPSession();
    console.log('CDP session created. Verifying content...');

    // CONTENT VERIFICATION (CodeMirror 6 / Media App)
    // We convert the verify function to a string to pass it to the browser.
    const expression =
        `(${verifyContentInBrowser.toString()})(${UI_INTERACTION_TIMEOUT})`;

    const evaluationResult = await textAppSession.send('Runtime.evaluate', {
      awaitPromise: true,
      returnByValue: true,
      expression: expression,
    });

    if (evaluationResult.exceptionDetails) {
      throw new Error(
          'Browser-side error: ' +
          evaluationResult.exceptionDetails.exception.description);
    }

    const content = evaluationResult.result.value;

    if (content.trim() !== FILE_CONTENT) {
      throw new Error(
          `File content mismatch! Expected "${FILE_CONTENT}", ` +
          `got "${content.trim()}"`);
    }
    console.log('File content verified successfully.');

    // Step 5: Cleanup
    // Update: Explicitly Close the Text App Window via raw CDP
    if (textAppSession) {
      await textAppSession.detach();
      textAppSession = null;
    }

    try {
      // Fetch all targets directly to bypass Puppeteer version mismatches
      const {targetInfos} = await browserSession.send('Target.getTargets');
      const textAppTargetInfo =
          targetInfos.find((t) => t.url === TEXT_APP_UI_URL);

      if (textAppTargetInfo) {
        await browserSession.send(
            'Target.closeTarget', {targetId: textAppTargetInfo.targetId});
        console.log('Closed Text app window (CDP Target closed).');
      } else {
        console.log('Text App target not found (might already be closed).');
      }
    } catch (e) {
      console.warn(
          'Warning: Could not close Text App target via CDP:', e.message);
    }

    console.log(`Cleaning up: Deleting "${FILE_NAME}"...`);
    await filesAppPage.bringToFront();

    try {
      // 1. Re-acquire the file row
      const fileRow = await filesAppPage.waitForSelector(
          fileSelector, {visible: true, timeout: UI_INTERACTION_TIMEOUT});

      // 2. Select the file
      try {
        await fileRow.click();
      } catch (clickError) {
        console.warn('Standard click failed. Attempting JS force click...');
        await filesAppPage.$eval(fileSelector, (el) => el.click());
      }

      // Give UI a moment to process selection
      await new Promise((r) => setTimeout(r, 500));

      // 3. Trigger Delete via Keyboard
      console.log('Sending "Delete" keystroke...');
      await filesAppPage.keyboard.press('Delete');

      // 4. Handle Deletion (Wait for Dialog OR File Disappearance)
      try {
        // Wait briefly for a confirmation dialog (e.g. 3 seconds)
        // If it appears, we click OK. If not, assume it went to trash.
        const confirmBtn = await filesAppPage.waitForSelector(
            '.cr-dialog-ok', {visible: true, timeout: 3000});
        console.log('Confirmation dialog detected. Clicking OK...');
        await confirmBtn.click();
      } catch (e) {
        console.log(
            'No confirmation dialog appeared (likely moved to Trash ' +
            'directly). Checking file absence...');
      }

      // 5. Final verification: Ensure the file row is GONE from the list
      await filesAppPage.waitForSelector(
          fileSelector, {hidden: true, timeout: UI_INTERACTION_TIMEOUT});

      console.log('File deletion confirmed (File row is gone). Test passed!');

    } catch (cleanupError) {
      console.error(`Cleanup failed: ${cleanupError.message}`);
    }

  } catch (error) {
    console.error(
        Colors.red, '--- An error occurred during the test', Colors.normal);
    console.error(error);
    if (filesAppPage) await takeScreenshot(filesAppPage, 'error_files_app.png');
  } finally {
    console.log('Shutting down...');
    if (browser) {
      if (textAppSession?.connection()) {
        try {
          await textAppSession.detach();
        } catch (e) {
        }
      }
      const allPages = await browser.pages();
      for (const page of allPages) {
        try {
          await page.close();
        } catch (e) {
        }
      }
      await browser.disconnect();
    }
  }
})();
