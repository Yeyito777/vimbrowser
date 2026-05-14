// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Playwright Test Script for End-to-End File Download and
 * Verification (TypeScript).
 *
 * This script connects to a remote ChromeOS instance, downloads a text file,
 * verifies it appears in the Files app, opens it in the default Text/Media app,
 * and verifies the content matches.
 */

import * as fs from 'fs';
import * as path from 'path';
import {Browser, BrowserContext, CDPSession, chromium, Page} from 'playwright';

// Constants
const FILE_CONTENT = 'Hello, ChromeOS E2E Testing!';
const FILE_BASE_NAME = 'test-download';
const FILE_NAME_DEFAULT = 'test-download.txt';

const ENCODED_CONTENT = encodeURIComponent(FILE_CONTENT);
const DOWNLOAD_PAGE_URL =
    `data:text/html,<a id="downloadLink" ` +
    `href="data:text/plain;charset=utf-8,${ENCODED_CONTENT}" ` +
    `download="${FILE_NAME_DEFAULT}">Download Test File</a>`;
const FILES_APP_URL = 'chrome://file-manager';
// The ID for the ChromeOS Media App (Backlight) which handles text files by
// default.
const TEXT_APP_UI_URL =
    'chrome-extension://mmfbcljfglbokpmkimbfghdkjmjhdgbg/index.html';
const UI_INTERACTION_TIMEOUT = 30000;

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
 * Helper to execute code in a specific CDP target (page/app/extension).
 * NOTE: Uses flatten: false to ensure Target.sendMessageToTarget works
 * correctly.
 */
async function evaluateOnTarget(
    hostSession: CDPSession, targetUrl: string, expression: string,
    timeoutMs: number = 30000): Promise<any> {
  const {targetInfos} = await hostSession.send('Target.getTargets' as any);
  const target = (targetInfos as any[]).find((t) => t.url === targetUrl);
  if (!target) throw new Error(`Target not found: ${targetUrl}`);

  // FIX: Set flatten to false to allow Target.sendMessageToTarget usage
  // (nested mode).
  const {sessionId} = await hostSession.send('Target.attachToTarget' as any, {
    targetId: target.targetId,
    flatten: false
  } as any);

  const msgId = Math.floor(Math.random() * 100000);
  const message = JSON.stringify({
    id: msgId,
    method: 'Runtime.evaluate',
    params: {expression, awaitPromise: true, returnByValue: true}
  });

  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      hostSession.off('Target.receivedMessageFromTarget', handler);
      reject(
          new Error(`Timeout waiting for evaluation on target ${targetUrl}`));
    }, timeoutMs);

    const handler = ({sessionId: eventSessionId, message: eventMessage}:
                         any) => {
      if (eventSessionId !== sessionId) return;
      try {
        const data = JSON.parse(eventMessage);
        if (data.id === msgId) {
          clearTimeout(timer);
          hostSession.off('Target.receivedMessageFromTarget', handler);

          if (data.error) {
            reject(new Error(`Protocol Error: ${JSON.stringify(data.error)}`));
          } else if (data.result && data.result.exceptionDetails) {
            reject(new Error(`Evaluation Error: ${
                data.result.exceptionDetails.exception.description}`));
          } else if (data.result && data.result.result) {
            resolve(data.result.result.value);
          } else {
            resolve(null);
          }
        }
      } catch (e) {
      }
    };

    hostSession.on('Target.receivedMessageFromTarget', handler);
    hostSession
        .send('Target.sendMessageToTarget' as any, {sessionId, message} as any)
        .catch((e) => {
          clearTimeout(timer);
          hostSession.off('Target.receivedMessageFromTarget', handler);
          reject(e);
        });
  });
}

function verifyContentInBrowser(timeoutMs: number) {
  return new Promise((resolve, reject) => {
    const startTime = Date.now();
    function poll() {
      try {
        // Strategy 1: CodeMirror Editor (common in web-based text editors).
        const editorDom = document.querySelector('.cm-editor');
        if (editorDom && (editorDom as any).cmView &&
            (editorDom as any).cmView.view) {
          const text = (editorDom as any).cmView.view.state.doc.toString();
          if (text && text.trim().length > 0) {
            resolve(text);
            return;
          }
        }
        // Strategy 2: Standard content container.
        const contentNode = document.querySelector('.cm-content');
        if (contentNode) {
          const text = (contentNode as HTMLElement).innerText;
          if (text && text.trim().length > 0) {
            resolve(text);
            return;
          }
        }
      } catch (e) {
      }
      if (Date.now() - startTime > timeoutMs) {
        reject(new Error('Timed out verifying content'));
        return;
      }
      setTimeout(poll, 500);
    }
    poll();
  });
}

async function runTest() {
  const port = process.argv[2] || '9222';
  const browserURL = `http://localhost:${port}`;

  let browser: Browser|null = null;
  let context: BrowserContext|null = null;
  let downloadPage: Page|null = null;
  let filesAppPage: Page|null = null;
  let mainCDPSession: CDPSession|null = null;
  let actualFileName: string = FILE_NAME_DEFAULT;

  try {
    console.log(`Connecting to browser at ${browserURL}...`);
    // Increased timeout for connection.
    browser = await chromium.connectOverCDP(browserURL, {timeout: 60000});

    if (browser.contexts().length > 0) {
      context = browser.contexts()[0];
      console.log('Attached to existing User Profile context.');
    } else {
      context = await browser.newContext();
      console.log('Warning: Created new context.');
    }

    // --- Step 2: Download the file ---
    console.log('Navigating to test page to download file...');
    downloadPage = await context.newPage();

    // This ensures Chrome handles the download natively, bypassing
    // Playwright's interception.
    console.log('Resetting download behavior to browser default...');
    const client = await downloadPage.context().newCDPSession(downloadPage);
    await client.send('Page.setDownloadBehavior', {behavior: 'default'});

    await downloadPage.goto(DOWNLOAD_PAGE_URL);

    console.log(`Clicked download link for "${FILE_NAME_DEFAULT}".`);
    await downloadPage.click('#downloadLink');

    // Wait for download to complete on the filesystem.
    console.log('Waiting for file system write (10s)...');
    await downloadPage.waitForTimeout(10000);
    await downloadPage.close();

    // --- Step 3: Launch Files App ---
    console.log('Launching Files app...');
    const managementPage = await context.newPage();
    mainCDPSession = await context.newCDPSession(managementPage);

    await mainCDPSession.send(
        'PWA.launch' as any, {manifestId: FILES_APP_URL} as any);

    filesAppPage = await context.waitForEvent('page', {
      predicate: (p) => p.url().startsWith(FILES_APP_URL),
      timeout: 15000
    });

    await filesAppPage.setViewportSize({width: 1280, height: 800});
    await filesAppPage.bringToFront();
    console.log('Switched to Files App window.');

    // Find and click "Downloads" folder.
    const downloadsLabel =
        filesAppPage.locator('#directory-tree [role="treeitem"]', {
                      hasText: 'Downloads'
                    }).first();
    await downloadsLabel.waitFor(
        {state: 'visible', timeout: UI_INTERACTION_TIMEOUT});
    await downloadsLabel.click();
    console.log('Clicked "Downloads" folder.');

    // --- Step 4: Find the File (Robust Dynamic Search) ---
    // We search for any file starting with 'test-download' to handle
    // 'test-download (1).txt' cases.
    const fileSelectorPattern = `[file-name^="${FILE_BASE_NAME}"]`;
    const fileRow = filesAppPage.locator(fileSelectorPattern).first();

    try {
      await fileRow.waitFor({state: 'visible', timeout: 5000});
      actualFileName =
          await fileRow.getAttribute('file-name') || FILE_NAME_DEFAULT;
      console.log(`Found "${actualFileName}" in Files app.`);
    } catch (e) {
      console.log('File not found immediately. Triggering Refresh (Ctrl+R)...');
      await filesAppPage.keyboard.press('Control+r');
      await filesAppPage.waitForTimeout(1000);

      try {
        await fileRow.waitFor(
            {state: 'visible', timeout: UI_INTERACTION_TIMEOUT});
        actualFileName =
            await fileRow.getAttribute('file-name') || FILE_NAME_DEFAULT;
        console.log(`Found "${actualFileName}" after refresh.`);
      } catch (finalError) {
        const visibleFileNames =
            await filesAppPage.locator('[file-name]').evaluateAll((els) => {
              return els.map((e) => e.getAttribute('file-name'));
            });
        throw new Error(`Could not find any file starting with "${
            FILE_BASE_NAME}". Visible: [${visibleFileNames.join(', ')}]`);
      }
    }

    // --- Step 5: Open and Verify ---
    console.log(
        `Double-clicking "${actualFileName}" to open Text/Media app...`);
    // Ensure we are clicking the row that corresponds to the found file name.
    const targetRow = filesAppPage.locator(`[file-name="${actualFileName}"]`);
    await targetRow.dblclick();

    // Wait for Media App to open.
    await new Promise((r) => setTimeout(r, 5000));
    console.log('Switched to Media App window (conceptually).');

    const expression =
        `(${verifyContentInBrowser.toString()})(${UI_INTERACTION_TIMEOUT})`;

    try {
      const content = await evaluateOnTarget(
          mainCDPSession, TEXT_APP_UI_URL, expression);

      if (!content || typeof content !== 'string') {
        throw new Error('Content retrieval returned null or non-string.');
      }

      if (content.trim() !== FILE_CONTENT) {
        throw new Error(`File content mismatch! Expected "${
            FILE_CONTENT}", got "${content.trim()}"`);
      }
      console.log(
          COLORS.green, 'File content verified successfully.', COLORS.normal);
    } catch (err: any) {
      throw new Error(`Text App Verification Failed: ${err.message}`);
    }

    // --- Step 6: Cleanup ---
    // Close Media App.
    try {
      const {targetInfos} =
          await mainCDPSession.send('Target.getTargets' as any);
      const textTarget =
          (targetInfos as any[]).find((t) => t.url === TEXT_APP_UI_URL);
      if (textTarget) {
        await mainCDPSession.send('Target.closeTarget' as any, {
          targetId: textTarget.targetId
        } as any);
        console.log('Closed Text app window.');
      }
    } catch (e: any) {
      console.warn('Warning: Could not close Text App target:', e.message);
    }

    // Delete File.
    console.log(`Cleaning up: Deleting "${actualFileName}"...`);
    await filesAppPage.bringToFront();

    await targetRow.waitFor({state: 'visible'});
    await targetRow.click();
    await filesAppPage.waitForTimeout(500);  // Wait for selection.

    console.log('Sending "Delete" keystroke...');
    await filesAppPage.keyboard.press('Delete');

    // Handle Confirmation Dialog.
    // Note: The class might vary depending on OS version, trying both common
    // selectors.
    try {
      const confirmBtn =
          filesAppPage.locator('.cr-dialog-ok, button.action-button').first();
      if (await confirmBtn.isVisible({timeout: 3000})) {
        console.log('Confirmation dialog detected. Clicking OK...');
        await confirmBtn.click();
      }
    } catch (e) {
      console.log(
          'No confirmation dialog appeared (likely moved to Trash directly).');
    }

    // Verify deletion.
    await targetRow.waitFor({state: 'hidden', timeout: UI_INTERACTION_TIMEOUT});
    console.log(
        COLORS.green, 'File deletion confirmed. Test passed!', COLORS.normal);

    await managementPage.close();

  } catch (error: any) {
    console.error(
        COLORS.red, '--- An error occurred during the test ---', COLORS.normal);
    console.error(error);

    if (filesAppPage) {
      try {
        const screenshotDir = path.join(__dirname, 'screenshots');
        if (!fs.existsSync(screenshotDir)) fs.mkdirSync(screenshotDir);
        const screenshotPath =
            path.join(screenshotDir, 'error_files_app_ts.png');
        await filesAppPage.screenshot({path: screenshotPath});
        console.log(
            COLORS.yellow, `Saved screenshot to ${screenshotPath}`,
            COLORS.normal);
      } catch (e) { /* ignore */
      }
    }
    process.exitCode = 1;
  } finally {
    console.log('Shutting down...');
    if (browser) {
      await browser.close();
    }
  }
}

runTest();
