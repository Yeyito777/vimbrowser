// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Clicks on the CDMs tab button and checks if "Robustness: Hardware Secure"
 * is present.
 */
async function checkL1Status() {
  const cdmsButton = document.getElementById('cdms-tab-button');
  if (cdmsButton) {
    console.log('Clicking CDMs tab button...');
    cdmsButton.click();
    // buffer time for UI update
    await new Promise((r) => setTimeout(r, 5000));
  } else {
    console.error('CDMs tab button not found!');
  }

  const tds = document.querySelectorAll('td');
  for (const td of tds) {
    if (td.textContent.trim() === 'Robustness') {
      const nextTd = td.nextElementSibling;
      if (
        nextTd &&
        nextTd.tagName === 'TD' &&
        nextTd.textContent.trim() === 'Hardware Secure'
      ) {
        console.log('Found Robustness: Hardware Secure');
        return true;
      }
    }
  }
  /* ... */
  console.log(
      'Robustness: Hardware Secure NOT found. Dumping text content of all TDs:',
  );
  const tdTexts = Array.from(tds).map((td) => td.textContent.trim());
  console.log(JSON.stringify(tdTexts));
  return false;
}

if (await checkL1Status()) {
  console.log('L1 Status check passed.');
} else {
  throw new Error('L1 Status check failed: Robustness is not Hardware Secure.');
}
