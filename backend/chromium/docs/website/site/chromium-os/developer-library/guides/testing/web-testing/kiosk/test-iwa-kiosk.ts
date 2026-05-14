// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Playwright Test Script for an IWA in Kiosk Mode.
 * This script connects to a remote ChromeOS instance in Kiosk mode, finds the
 * IWA target (which can be of type 'other' or 'app'), and interacts with its UI
 * using low-level CDP commands tunneled through a browser session.
 */

import {Browser, CDPSession, chromium} from 'playwright';

// --- FIX 1: Declare document to satisfy TypeScript ---
declare const document: any;
// ----------------------------------------------------

const IWA_WEB_BUNDLE_ID =
    'aiv4bxauvcu3zvbu6r5yynoh4atkzqqaoeof5mwz54b4zfywcrjuoaacai';
const IWA_URL_PATTERN = `isolated-app://${IWA_WEB_BUNDLE_ID}`;

// Color constants for console output.
const COLORS = {
  green: '\x1b[32m',
  red: '\x1b[31m',
  yellow: '\x1b[33m',
  reset: '\x1b[m',
};

/**
 * A wrapper class to handle communicating with a specific Target via a Host
 * Session. This mimics the direct CDPSession interface but tunnels messages
 * using `Target.sendMessageToTarget` to bypass Playwright's target filtering.
 */
class RawCDPSession {
  constructor(private hostSession: CDPSession, private sessionId: string) {}

  /**
   * Sends a CDP command to the specific target.
   */
  async send(method: string, params: any = {}): Promise<any> {
    const msgId = Math.floor(Math.random() * 1000000);
    const message = JSON.stringify({id: msgId, method, params});

    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.hostSession.off('Target.receivedMessageFromTarget', handler);
        reject(new Error(`Timeout waiting for command ${method} on target`));
      }, 15000);

      const handler = ({sessionId, message: responseMessage}: any) => {
        if (sessionId !== this.sessionId) return;

        try {
          const data = JSON.parse(responseMessage);
          if (data.id === msgId) {
            clearTimeout(timeout);
            this.hostSession.off('Target.receivedMessageFromTarget', handler);

            if (data.error) {
              reject(
                  new Error(`${method} failed: ${JSON.stringify(data.error)}`));
            } else {
              resolve(data.result);
            }
          }
        } catch (e) {
          // Ignore parsing errors for other messages.
        }
      };

      this.hostSession.on('Target.receivedMessageFromTarget', handler);

      this.hostSession
          .send('Target.sendMessageToTarget' as any, {
            sessionId: this.sessionId,
            message
          } as any)
          .catch((err) => {
            clearTimeout(timeout);
            this.hostSession.off('Target.receivedMessageFromTarget', handler);
            reject(err);
          });
    });
  }
}

// --- Interaction Helper Functions ---

async function clickElement(session: RawCDPSession, nodeId: number) {
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

async function typeText(session: RawCDPSession, nodeId: number, text: string) {
  await session.send('DOM.focus', {nodeId});
  for (const char of text) {
    await session.send('Input.dispatchKeyEvent', {type: 'char', text: char});
  }
}

async function waitForCondition(
    session: RawCDPSession, pageFunction: Function, timeout: number,
    ...args: any[]): Promise<void> {
  const startTime = Date.now();
  while (Date.now() - startTime < timeout) {
    // pageFunction is serialized and evaluated in the browser context.
    const expression = `(${pageFunction.toString()})(${
        args.map((a) => JSON.stringify(a)).join(',')})`;

    const {result} = await session.send('Runtime.evaluate', {
      expression,
      returnByValue: true,
    });

    if (result.value) return;
    await new Promise((resolve) => setTimeout(resolve, 500));
  }
  throw new Error(`waitForCondition timed out after ${timeout}ms`);
}

async function runKioskTest() {
  const port = process.argv[2] || '9222';
  const browserURL = `http://localhost:${port}`;

  let browser: Browser|null = null;
  // Note: We removed hostPage because we are using a browser-level session.

  try {
    console.log(`Connecting to browser at ${browserURL}...`);
    browser = await chromium.connectOverCDP(browserURL);

    // --- FIX 2: Use Browser-level CDP Session ---
    const hostSession = await browser.newBrowserCDPSession();
    console.log('Established Browser-level CDP session.');

    console.log('Searching for IWA target...');

    // 2. Find the IWA target using Target.getTargets.
    const {targetInfos} = await hostSession.send('Target.getTargets' as any);

    // --- FIX 3: Loosen type check to accept 'app', 'page', or 'other' ---
    const iwaTargetInfo = (targetInfos as any[]).find(
        (t) => t.url.startsWith(IWA_URL_PATTERN) &&
            ['app', 'page', 'other'].includes(t.type));

    if (!iwaTargetInfo) {
      const visibleTargets =
          (targetInfos as any[]).map((t) => `${t.type}: ${t.url}`).join('\n');
      throw new Error(
          `Could not find IWA target. Available targets:\n${visibleTargets}`);
    }

    console.log(
        COLORS.green,
        `Found IWA Target: ${iwaTargetInfo.url} (Type: ${iwaTargetInfo.type})`,
        COLORS.reset);

    // 3. Attach to the IWA target.
    // IMPORTANT: flatten: false is required to use Target.sendMessageToTarget.
    const {sessionId} =
        await hostSession.send('Target.attachToTarget' as any, {
          targetId: iwaTargetInfo.targetId,
          flatten: false
        } as any);

    const iwaSession = new RawCDPSession(hostSession, sessionId);
    console.log('Attached to IWA via tunnelled session.');

    // 4. Initialize Domains.
    await iwaSession.send('Runtime.enable');
    await iwaSession.send('DOM.enable');

    // 5. Interact with DOM.
    const {root} = await iwaSession.send('DOM.getDocument');

    // -- Click "Create new socket connection" --
    console.log('Waiting for \'Create new socket connection\' button...');

    // Simple polling for the button.
    let addSocketButtonNodeId = 0;
    for (let i = 0; i < 10; i++) {
      const res = await iwaSession.send('DOM.querySelector', {
        nodeId: root.nodeId,
        selector: '#addSocketButton',
      });
      if (res.nodeId) {
        addSocketButtonNodeId = res.nodeId;
        break;
      }
      await new Promise((r) => setTimeout(r, 1000));
    }

    if (!addSocketButtonNodeId) {
      throw new Error('Could not find #addSocketButton');
    }

    await clickElement(iwaSession, addSocketButtonNodeId);
    console.log('Clicked \'Create new socket connection\' button.');

    // -- Shadow DOM Logic --
    console.log('Waiting for socket components...');
    // Static wait for components to render.
    await new Promise((r) => setTimeout(r, 2000));

    // Find Components.
    const {nodeId: leftCompNodeId} =
        await iwaSession.send('DOM.querySelector', {
          nodeId: root.nodeId,
          selector: 'socket-connection',
        });
    if (!leftCompNodeId) {
      throw new Error('Could not find socket-connection component.');
    }

    const {nodeId: rightCompNodeId} =
        await iwaSession.send('DOM.querySelector', {
          nodeId: root.nodeId,
          selector: 'socket-server',
        });
    if (!rightCompNodeId) {
      throw new Error('Could not find socket-server component.');
    }

    // Get Shadow Roots.
    const {node: leftCompNode} =
        await iwaSession.send('DOM.describeNode', {nodeId: leftCompNodeId});
    const leftShadowRootNodeId = leftCompNode.shadowRoots?.[0]?.nodeId;

    const {node: rightCompNode} =
        await iwaSession.send('DOM.describeNode', {nodeId: rightCompNodeId});
    const rightShadowRootNodeId = rightCompNode.shadowRoots?.[0]?.nodeId;

    if (!leftShadowRootNodeId || !rightShadowRootNodeId) {
      throw new Error('Could not access shadow roots of socket components.');
    }
    console.log('Found shadow roots.');

    const inputShadowSelector = 'input#messageInput';
    const sendButtonShadowSelector = 'button#sendButton';

    // -- Scenario 1: Left -> Right --
    console.log('Sending message from LEFT to RIGHT...');
    const {nodeId: leftInputId} = await iwaSession.send('DOM.querySelector', {
      nodeId: leftShadowRootNodeId,
      selector: inputShadowSelector,
    });
    const {nodeId: leftSendBtnId} = await iwaSession.send('DOM.querySelector', {
      nodeId: leftShadowRootNodeId,
      selector: sendButtonShadowSelector,
    });

    await typeText(iwaSession, leftInputId, 'Sing me a song!');
    await clickElement(iwaSession, leftSendBtnId);

    console.log('Verifying message on RIGHT...');
    await waitForCondition(
        iwaSession,
        (selector: string, text: string) => {
          const root = document.querySelector(selector);
          return root && root.shadowRoot &&
              root.shadowRoot.innerHTML.includes(text);
        },
        15000, 'socket-server', 'Sing me a song!');
    console.log(
        COLORS.green, 'Verified \'Sing me a song!\' on right.', COLORS.reset);

    // -- Scenario 2: Right -> Left --
    console.log('Sending message from RIGHT to LEFT...');
    const {nodeId: rightInputId} = await iwaSession.send('DOM.querySelector', {
      nodeId: rightShadowRootNodeId,
      selector: inputShadowSelector,
    });
    const {nodeId: rightSendBtnId} =
        await iwaSession.send('DOM.querySelector', {
          nodeId: rightShadowRootNodeId,
          selector: sendButtonShadowSelector,
        });

    await typeText(iwaSession, rightInputId, 'Hello from the right side!');
    await clickElement(iwaSession, rightSendBtnId);

    console.log('Verifying message on LEFT...');
    await waitForCondition(
        iwaSession,
        (selector: string, text: string) => {
          const root = document.querySelector(selector);
          return root && root.shadowRoot &&
              root.shadowRoot.innerHTML.includes(text);
        },
        15000, 'socket-connection', 'Hello from the right side!');
    console.log(
        COLORS.green, 'Verified \'Hello from the right side!\' on left.',
        COLORS.reset);

    console.log(COLORS.green, 'Test finished successfully!', COLORS.reset);

  } catch (error: any) {
    console.error(
        COLORS.red, 'An error occurred during the Kiosk test:', error.message,
        COLORS.reset);
    process.exitCode = 1;
  } finally {
    // We only need to close the browser connection.
    if (browser) await browser.close();
  }
}

runKioskTest();
