// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/desktop_capture.h"

#include "base/feature_list.h"
#include "build/build_config.h"
#include "content/browser/media/capture/pip_screen_capture_coordinator.h"
#include "content/browser/renderer_host/media/media_stream_manager.h"
#include "content/browser/renderer_host/media/video_capture_manager.h"
#include "content/common/features.h"
#include "content/public/common/content_features.h"




#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"

// CGDisplayStreamCreate() is marked as deprecated from macOS 14 (Sonoma), so
// don't use unless the feature flag is set.
bool CGDisplayStreamCreateIsAvailable() {
  if (base::mac::MacOSMajorVersion() >= 14) {
    return false;
  }
  return true;
}
#endif  // BUILDFLAG(IS_MAC)

namespace content::desktop_capture {


webrtc::DesktopCaptureOptions CreateDesktopCaptureOptions() {
  auto options = webrtc::DesktopCaptureOptions::CreateDefault();
  // Leave desktop effects enabled during WebRTC captures.
  options.set_disable_effects(false);
#if BUILDFLAG(IS_MAC)
  // Enabling IO surface capturer means that we will be using the
  // CGDisplayStreamCreate() API. This is marked as deprecated from macOS 14
  // (Sonoma), only use it if it's available.
  if (base::FeatureList::IsEnabled(features::kIOSurfaceCapturer) &&
      CGDisplayStreamCreateIsAvailable()) {
    options.set_allow_iosurface(true);
  }
#endif
  return options;
}

std::unique_ptr<webrtc::DesktopCapturer> CreateScreenCapturer(
    webrtc::DesktopCaptureOptions options,
    bool for_snapshot) {

  return webrtc::DesktopCapturer::CreateScreenCapturer(options);
}

std::unique_ptr<webrtc::DesktopCapturer> CreateWindowCapturer(
    webrtc::DesktopCaptureOptions options) {
#if defined(RTC_ENABLE_WIN_WGC)
  options.set_allow_wgc_capturer_fallback(true);
#endif  // defined(RTC_ENABLE_WIN_WGC)

  return webrtc::DesktopCapturer::CreateWindowCapturer(options);
}

bool CanUsePipeWire() {
  return false;
}

bool ShouldEnumerateCurrentProcessWindows() {
  return true;
}

void OpenNativeScreenCapturePicker(
    content::DesktopMediaID::Type type,
    base::OnceCallback<void(DesktopMediaID::Id)> created_callback,
    base::OnceCallback<void(webrtc::DesktopCapturer::Source)> picker_callback,
    base::OnceCallback<void()> cancel_callback,
    base::OnceCallback<void()> error_callback) {
  content::MediaStreamManager::GetInstance()
      ->video_capture_manager()
      ->OpenNativeScreenCapturePicker(
          type, std::move(created_callback), std::move(picker_callback),
          std::move(cancel_callback), std::move(error_callback));
}

void CloseNativeScreenCapturePicker(DesktopMediaID source_id) {
  content::MediaStreamManager::GetInstance()
      ->video_capture_manager()
      ->CloseNativeScreenCapturePicker(source_id);
}

std::optional<DesktopMediaID::Id> GetPipWindowToExcludeFromScreenCapture(
    DesktopMediaID::Id desktop_id) {
  if (auto* coordinator = content::PipScreenCaptureCoordinator::GetInstance()) {
    return coordinator->GetPipWindowToExcludeFromScreenCapture(desktop_id);
  }

  return std::nullopt;
}

}  // namespace content::desktop_capture
