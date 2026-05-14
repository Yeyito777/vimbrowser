// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const video = document.querySelector('video');
if (video) {
  const quality = video.getVideoPlaybackQuality();
  const droppedFramesCount = quality.droppedVideoFrames;
  const totalFrames = quality.totalVideoFrames;
  let droppedFramesPercent = 0;
  if (totalFrames > 0) {
    droppedFramesPercent = (droppedFramesCount / totalFrames) * 100;
  }

  performance.mark('dropped-frames-count', {
    detail: droppedFramesCount,
  });
  performance.mark('dropped-frames-percent', {
    detail: droppedFramesPercent,
  });
}
