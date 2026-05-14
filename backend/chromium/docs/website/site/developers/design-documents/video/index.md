---
breadcrumbs:
- - /developers
  - For Developers
- - /developers/design-documents
  - Design Documents
page_name: video
title: Audio / Video Playback
---

Interested in audio and video? Please see the main [audio/video](/audio-video)
page for more information on contributing and code locations.

> This document preserves a lot of historical information from the original
> implementation of HTML5 audio and video in Chromium, and contains references
> to outdated terminology, features, and structures (for example, Webkit instead
> of Blink).
>
> For the most up-to-date implementation details, please see the
> [media/README.md](https://chromium.googlesource.com/chromium/src/+/HEAD/media/README.md)
> in the source tree.
>

## Documentation

- [HTMLAudioElement](https://html.spec.whatwg.org/#htmlaudioelement)
- [HTMLMediaElement](https://html.spec.whatwg.org/#htmlmediaelement)
- [HTMLVideoElement](https://html.spec.whatwg.org/#htmlvideoelement)

## Overview

Chromium's media playback implementation is divided into several major
components spanning multiple processes. At a high level, it involves:

- **Blink (Renderer Process):**
  - [`HTMLMediaElement`](https://source.chromium.org/chromium/chromium/src/+/main:third_party/blink/renderer/core/html/media/html_media_element.h):
    Implements the HTML and JavaScript bindings specified by WHATWG.
  - [`WebMediaPlayerImpl`](https://source.chromium.org/chromium/chromium/src/+/main:third_party/blink/renderer/platform/media/web_media_player_impl.h):
    The bridge between Blink and the underlying media pipeline.
- **Media Pipeline:**
  - [`Pipeline`](https://source.chromium.org/chromium/chromium/src/+/main:media/base/pipeline.h):
    The central controller that orchestrates the state of media playback
    (Starting, Stopping, Seeking).
  - **Demuxers:** Parse container formats (ISO BMFF, WebM, Ogg, etc.) to extract
    elementary streams.
  - **Decoders:** Convert encoded streams into raw audio/video frames.
    - **Software:** FFmpeg, Libvpx (VP8/VP9), Libaom (AV1).
    - **Hardware:** Accelerated video decoding is handled by platform-specific
      implementations (e.g., Vaapi, D3D11, MediaCodec) often running in the GPU
      Process or a dedicated Media Service.
  - **Renderers:** Sync audio and video streams to a clock and present them to
    the user.

## Pipeline

The pipeline is a pull-based media playback engine that abstracts each step of
media playback into (at least) 6 different filters: data source, demuxing, audio
decoding, video decoding, audio rendering, and video rendering. The pipeline
manages the lifetime of the renderer and exposes a thread safe interface to
clients. The filters are connected together to form a filter graph.

### Design goals

- Use Chromium threading constructs such as
  [TaskRunner](https://source.chromium.org/chromium/chromium/src/+/main:base/task/task_runner.h)

- Filters do not determine threading model

- All filter actions are asynchronous and use callbacks to signal completion

- Upstream filters are oblivious to downstream filters (i.e., DataSource is
  unaware of Demuxer)

- Prefer explicit types and methods over general types and methods (i.e., prefer
  `foo->Bar()` over `foo->SendMessage(MSG_BAR)`)

- Can run inside security sandbox

- Runs on Windows, Mac and Linux on x86 and ARM

- Supports arbitrary audio/video codecs

### Design non-goals

- Dynamic loading of filters via shared libraries

- Buffer management negotiation

- Building arbitrary filter graphs

- Supporting filters beyond the scope of media playback

### Original research findings

The original research into supporting video in Chromium started in
September 2008. Before deciding to implement our own media playback engine we
considered the following alternative technologies:

- DirectShow (Windows specific, cannot run inside sandbox without major hacking)

- GStreamer (Windows support questionable at the time, extra ~2MB of DLLs due to
  library dependencies, targets many of our non-goals)

- VLC (cannot use due to GPL)

- MPlayer (cannot use due to GPL)

- OpenMAX (complete overkill for our purposes)

- liboggplay (specific to Ogg Theora/Vorbis)

Our approach was to write our own media playback engine that was audio/video
codec agnostic and focused on playback. Using FFmpeg avoids both the use of
proprietary/commercial codecs and allows Chromium's media engine to support a
wide variety of formats depending on FFmpeg's build configuration.

### The pipeline design

![Media pipeline diagram](./video_stack_arch.png)

As previously mentioned, the pipeline is completely pull-based and relies on the
sound card to drive playback. As the sound card requests additional data, the
[audio renderer](https://source.chromium.org/chromium/chromium/src/+/main:media/base/audio_renderer.h)
requests decoded audio data from the
[audio decoder](https://source.chromium.org/chromium/chromium/src/+/main:media/base/audio_decoder.h),
which requests encoded buffers from the
[demuxer](https://source.chromium.org/chromium/chromium/src/+/main:media/base/demuxer.h),
which reads from the
[data source](https://source.chromium.org/chromium/chromium/src/+/main:media/base/data_source.h),
and so on. As decoded audio data data is fed into the sound card the pipeline's
global clock is updated. The
[video renderer](https://source.chromium.org/chromium/chromium/src/+/main:media/base/video_renderer.h)
polls the global clock upon each vsync to determine when to request decoded
frames from the
[video decoder](https://source.chromium.org/chromium/chromium/src/+/main:media/base/video_decoder.h)
and when to render new frames to the video display. In the absence of a sound
card or an audio track, the system clock is used to drive video decoding and
rendering. Relevant source code is in the
[`media/`](https://source.chromium.org/chromium/chromium/src/+/main:media/)
directory.

The pipeline uses a state machine to handle playback and events such as pausing,
seeking, and stopping. A state transition typically consists of notifying all
filters of the event and waiting for completion callbacks before completing the
transition (diagram from
[pipeline_impl.h](https://source.chromium.org/chromium/chromium/src/+/main:media/base/pipeline_impl.h)):

```c++
//   [ *Created ]                       [ Any State ]
//         | Start()                         | Stop()
//         V                                 V
//   [ Starting ]                       [ Stopping ]
//         |                                 |
//         V                                 V
//   [ Playing ] <---------.            [ Stopped ]
//     |  |  | Seek()      |
//     |  |  V             |
//     |  | [ Seeking ] ---'
//     |  |                ^
//     |  | *TrackChange() |
//     |  V                |
//     | [ Switching ] ----'
//     |                   ^
//     | Suspend()         |
//     V                   |
//   [ Suspending ]        |
//     |                   |
//     V                   |
//   [ Suspended ]         |
//     | Resume()          |
//     V                   |
//   [ Resuming ] ---------'
```

The pull-based design allows pause to be implemented by setting the playback
rate to zero, causing the audio and video renderers to stop requesting data from
upstream filters. Without any pending requests the entire pipeline enters an
implicit paused state.

## Integration

The following diagram shows the original integration of the media playback
pipeline into WebKit (now Blink) and the Chromium browser.

![Diagram showing original design of the video stack in Chromium](./video_stack_chrome.png)

### Diagram Legend

**`(1)`** WebKit requests to create a media player, which in Chromium's case
creates WebMediaPlayerImpl and Pipeline.

**`(2)`** BufferedDataSource requests to fetch the current video URL via
ResourceLoader.

**`(3)`** ResourceDispatcher forwards the request to the browser process.
**`(4)`** A URLRequest is created for the request, which may already have cached
data present in HttpCache. Data is sent back to BufferedDataSource as it becomes
available.

**`(5)`** FFmpeg demuxes and decodes audio/video data.

**`(6)`** Due to sandboxing, AudioRendererImpl cannot open an audio device
directly and requests the browser to open the device on its behalf.

**`(7)`** The browser opens a new audio device and forwards audio callbacks to
the corresponding render process.

**`(8)`** Invalidates are sent to WebKit as new frames are available.
