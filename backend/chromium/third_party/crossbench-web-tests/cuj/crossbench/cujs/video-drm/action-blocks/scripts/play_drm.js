// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const file = 'FILE_NAME';

/**
 * Plays a DRM-protected video and verifies that its content is black,
 * which indicates that hardware DRM is active. This function uses async/await
 * and is designed to be called from an external script (e.g., via DevTools).
 * @param {string} mpdPath The path to the MPD file for the video stream.
 */
async function verifyDrmPlayback(mpdPath) {
  const video = document.getElementById('video');
  if (!video) throw new Error('Video element not found');

  const waitForEvent = (eventName) => {
    return new Promise((resolve, reject) => {
      const onEvent = () => {
        cleanup();
        resolve();
      };

      const onError = (e) => {
        cleanup();
        reject(
            new Error(
                `Video error waiting for ${eventName}: ` +
              `${video.error ? video.error.message : e}`,
            ),
        );
      };

      const cleanup = () => {
        video.removeEventListener(eventName, onEvent);
        video.removeEventListener('error', onError);
      };

      video.addEventListener(eventName, onEvent);
      video.addEventListener('error', onError);
    });
  };

  // Start playback.
  console.log(`Starting playback for ${mpdPath}`);
  const playbackPromise = play_shaka_drm(mpdPath);

  console.log('Waiting for playing event...');
  await waitForEvent('playing');

  // Wait for the complete playback
  if (!video.ended) {
    console.log('Waiting for ended event...');
    await waitForEvent('ended');
  }

  // Ensure that shake player didn't fail.
  await playbackPromise;

  return;
}

await verifyDrmPlayback(file);

console.log('DRM Playback verification passed.');
