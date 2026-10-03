// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/webrtc/chrome_screen_enumerator.h"

#include <tuple>

#include "base/feature_list.h"
#include "base/lazy_instance.h"
#include "base/task/bind_post_task.h"
#include "build/build_config.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/desktop_media_id.h"
#include "content/public/common/content_features.h"
#include "third_party/blink/public/common/features_generated.h"
#include "third_party/blink/public/mojom/mediastream/media_stream.mojom.h"
#include "ui/display/screen.h"

#if BUILDFLAG(IS_LINUX)
#include "base/functional/callback.h"
#include "content/public/browser/desktop_capture.h"
#endif

#if BUILDFLAG(IS_LINUX)
namespace {
base::LazyInstance<std::unique_ptr<webrtc::DesktopCapturer>>::DestructorAtExit
    g_desktop_capturer_for_testing = LAZY_INSTANCE_INITIALIZER;
}  // namespace

#endif

namespace {
#if BUILDFLAG(IS_LINUX)
blink::mojom::StreamDevicesSetPtr EnumerateScreens(
    blink::mojom::MediaStreamType stream_type) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  blink::mojom::StreamDevicesSetPtr stream_devices_set =
      blink::mojom::StreamDevicesSet::New();

  std::unique_ptr<webrtc::DesktopCapturer> capturer =
      (g_desktop_capturer_for_testing.IsCreated())
          ? std::move(g_desktop_capturer_for_testing.Get())
          : content::desktop_capture::CreateScreenCapturer(
                content::desktop_capture::CreateDesktopCaptureOptions(),
                /*for_snapshot=*/true);
  if (!capturer) {
    return stream_devices_set;
  }

  capturer->Start(/*callback=*/nullptr);
  webrtc::DesktopCapturer::SourceList source_list;
  if (!capturer->GetSourceList(&source_list)) {
    return stream_devices_set;
  }

  for (const auto& source : source_list) {
    const std::string media_id =
        content::DesktopMediaID(content::DesktopMediaID::Type::TYPE_SCREEN,
                                source.id)
            .ToString();
    blink::MediaStreamDevice device(stream_type, media_id,
                                    /*name=*/"Screen",
                                    /*display_id=*/source.display_id);
    stream_devices_set->stream_devices.push_back(
        blink::mojom::StreamDevices::New(/*audio_device=*/std::nullopt,
                                         /*video_device=*/device));
  }

  return stream_devices_set;
}

#endif
}  // namespace

ChromeScreenEnumerator::ChromeScreenEnumerator() = default;

ChromeScreenEnumerator::~ChromeScreenEnumerator() = default;

#if BUILDFLAG(IS_LINUX)
void ChromeScreenEnumerator::SetDesktopCapturerForTesting(
    std::unique_ptr<webrtc::DesktopCapturer> capturer) {
  g_desktop_capturer_for_testing.Get() = std::move(capturer);
}

#endif  // BUILDFLAG(IS_LINUX)

void ChromeScreenEnumerator::EnumerateScreens(
    blink::mojom::MediaStreamType stream_type,
    ScreensCallback screens_callback) const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)
  content::GetUIThreadTaskRunner({})->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(::EnumerateScreens, stream_type),
      base::BindOnce(
          [](ScreensCallback screens_callback,
             blink::mojom::StreamDevicesSetPtr stream_devices_set) {
            std::move(screens_callback)
                .Run(*stream_devices_set,
                     stream_devices_set->stream_devices.size() > 0
                         ? blink::mojom::MediaStreamRequestResult::OK
                         : blink::mojom::MediaStreamRequestResult::NO_HARDWARE);
          },
          std::move(screens_callback)));
#else
  // TODO(crbug.com/40216442): Implement for other platforms than Chrome OS.
  NOTREACHED();
#endif
}
