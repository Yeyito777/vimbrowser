---
breadcrumbs: []
page_name: audio-video
title: Audio/Video
---

A starting point for becoming familiar with audio and video in the Chromium
projects.

## Get Connected

### Slack

For more real-time questions and collaboration, please consider joining the
`#media` slack channel.

See the main [Chromium Slack](/developers/slack) page for rules and instructions
on signing up, as well as tips and tricks.

### Mailing Groups

For more serious technical discussions or topics with potentially broader
interest, it's still best practice to send to the [chromium-dev](https://groups.google.com/a/chromium.org/g/chromium-dev)
discussion group ([chromium-dev@chromium.org](mailto:chromium-dev@chromium.org))
or, for media specific matters, the [media-dev](https://groups.google.com/a/chromium.org/forum/#!forum/media-dev)
discussion group ([media-dev@chromium.org](mailto:media-dev@chromium.org)).

See [Technical Discussion Groups](/developers/technical-discussion-groups) for
an extensive list of available groups.

### Issue Tracking

Issues are tracked in the [Chromium > Internals > Media](https://g-issues.chromium.org/components/1456190)
component. See the [open issues](https://g-issues.chromium.org/issues?q=status:open%20componentid:1456190)
query.

## Documentation

The majority of the media documentation lives in the source tree, see
[media/README.md](https://chromium.googlesource.com/chromium/src/+/HEAD/media/README.md).

For historical reference, here's the original
[design doc for HTML5 audio/video](/developers/design-documents/video).

## Codec and Container Support

Chromium supports a variety of codecs and containers for content playback
(decoding) and content capture (encoding), listed below.

### Container formats

* MP4 (QuickTime/ MOV / ISO-BMFF / CMAF)
* Ogg
* WebM
* WAV
* Matroska
* HLS

### Codec formats

#### Audio

* FLAC
* MP3
* Opus
* PCM 8-bit unsigned integer
* PCM 16-bit signed integer little endian
* PCM 32-bit float little endian
* PCM μ-law
* Vorbis

##### Proprietary Audio Codecs (Limited to Google Chrome)

* AAC (Main, LC, HE profiles only)
* xHE-AAC (requires native OS support, i.e. Android P+, macOS, Windows 11 22H2+)

#### Video

* AV1
* VP8
* VP9

##### Proprietary Video Codecs (Limited to Google Chrome)

* H.264 / AVC
* H.265 / HEVC (Proprietary, limited to Google Chrome, requires hardware support)

## Code Locations

### Chromium

* [`media/`](https://chromium.googlesource.com/chromium/src/+/HEAD/media/) -
    Home to all things media!

* [`media/audio`](https://chromium.googlesource.com/chromium/src/+/HEAD/media/audio/) -
    OS audio input/output abstractions

* [`media/video/capture`](https://chromium.googlesource.com/chromium/src/+/HEAD/media/video/capture/) -
    OS camera input abstraction

* [`media/video`](https://chromium.googlesource.com/chromium/src/+/HEAD/media/video/) -
    software/hardware video decoder interfaces + implementations

* [`third_party/ffmpeg`](https://chromium.googlesource.com/chromium/third_party/ffmpeg) -
    Chromium's copy of FFmpeg

* [`third_party/libvpx`](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/libvpx/) -
    Chromium's copy of libvpx

### Blink

* [`third_party/blink/renderer/core/html/media/html_media_element.{cpp,h,idl}`](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/renderer/core/html/media/html_media_element.h) -
    media element base class

* [`third_party/blink/renderer/core/html/media/html_audio_element.{cpp,h,idl}`](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/renderer/core/html/media/html_audio_element.h) -
    audio element implementation

* [`third_party/blink/renderer/core/html/media/html_video_element.{cpp,h,idl}`](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/renderer/core/html/media/html_video_element.h) -
    video element implementation

### Particularly Interesting Bits

* [`media/base/mime_util.cc`](https://chromium.googlesource.com/chromium/src/+/HEAD/media/base/mime_util.cc) -
    defines `canPlayType()` behaviour and file extension mapping

* [`third_party/blink/renderer/platform/media/multi_buffer_data_source.{cc,h}`](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/renderer/platform/media/multi_buffer_data_source.h) -
    Chromium's main implementation of DataSource for the media pipeline

* [`third_party/blink/renderer/platform/media/multi_buffer.{cc,h}`](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/renderer/platform/media/multi_buffer.h) -
    Implements the sliding window buffering strategy (see below)

* [`third_party/blink/public/platform/web_media_player.h`](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/public/platform/web_media_player.h) -
    Blink's media player interface for providing HTML5 audio/video functionality

* [`third_party/blink/renderer/platform/media/web_media_player_impl.{cc,h}`](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/renderer/platform/media/web_media_player_impl.h) -
    Chromium's main implementation of WebMediaPlayer

## How does everything get instantiated?

[`WebLocalFrameClient::CreateMediaPlayer()`](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/public/web/web_local_frame_client.h)
is the Blink embedder API for creating a
[`WebMediaPlayer`](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/public/platform/web_media_player.h)
and passing it back to Blink. Every HTML5 audio/video element will ask the
embedder to create a
[`WebMediaPlayer`](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/public/platform/web_media_player.h).

For Chromium this is handled in
[`RenderFrameImpl`](https://chromium.googlesource.com/chromium/src/+/HEAD/content/renderer/render_frame_impl.h).

## Build Configuration & GN Flags

Chromium currently uses the [GN](https://gn.googlesource.com/gn/) meta-build
system for generating build files. With several exceptions, the majority of
GN flags that alter the behavior of Chromium's HTML5 audio/video implementation
are specified in the [media_options.gni](https://chromium.googlesource.com/chromium/src/+/HEAD/media/media_options.gni)
file.

### `ffmpeg_branding`

Overrides which version of FFmpeg to use.

* Default: `$(branding)` - matches the build branding (see [is_chrome_branded](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/google_chrome_branded_builds.md))
* Values:
  * `Chrome` - includes additional proprietary codecs (MP3, etc..) for use
        with Google Chrome
  * `Chromium` - builds default set of codecs

### `proprietary_codecs`

Alters the list of codecs Chromium claims to support, which affects `<source>`
and `canPlayType()` behavior.

* Default: `false`
* Values:
  * `false` - `<source>` and `canPlayType()` assume the default set of codecs
  * `true` - `<source>` and `canPlayType()` assume they support additional
    proprietary codecs

## How buffering works

Chromium uses a combination of range requests and an in-memory sliding window to
buffer media. We have a low and high watermark that is used to determine when to
purposely stall the HTTP request and when to resume the HTTP request.

It's complicated, so here's a picture:

![Buffering Diagram](/audio-video/ChromiumMediaBuffering.png)
