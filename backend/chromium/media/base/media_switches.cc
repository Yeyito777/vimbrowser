// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// 2023-10-22: Newly added flags and features should document an owner and
// expiry condition. The expiry condition is freeform and could be a date,
// experiment, bug, the deletion of a file, etc.

#include "media/base/media_switches.h"

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "build/build_config.h"
#include "build/chromecast_buildflags.h"
#include "gpu/config/gpu_driver_bug_workarounds.h"
#include "gpu/config/gpu_finch_features.h"
#include "media/media_buildflags.h"
#include "ui/gl/gl_features.h"
#include "ui/gl/gl_utils.h"

#if BUILDFLAG(IS_LINUX)
#include "base/cpu.h"
#include "components/system_media_controls/linux/buildflags/buildflags.h"
#endif

#if BUILDFLAG(IS_MAC)
#include "base/mac/mac_util.h"
#endif


namespace switches {

namespace autoplay {

// Autoplay policy that requires a document user activation.
const char kDocumentUserActivationRequiredPolicy[] =
    "document-user-activation-required";

// Autoplay policy that does not require any user gesture.
const char kNoUserGestureRequiredPolicy[] = "no-user-gesture-required";

// Autoplay policy to require a user gesture in order to play.
const char kUserGestureRequiredPolicy[] = "user-gesture-required";

}  // namespace autoplay

// Allow users to specify a custom buffer size for debugging purpose.
const char kAudioBufferSize[] = "audio-buffer-size";

// Skip the permission prompt for Captured Surface Control.
const char kAutoGrantCapturedSurfaceControlPrompt[] =
    "auto-grant-captured-surface-control-prompt";

// Command line flag name to set the autoplay policy.
const char kAutoplayPolicy[] = "autoplay-policy";

// NOTE: callers should always use the free functions in
// /media/cast/encoding/encoding_support.h instead of accessing these features
// directly.
//
// TODO(crbug.com/286443864): Guard Cast Sender flags with !IS_ANDROID.
//
// If enabled, completely disables use of H264 hardware encoding for Cast
// Streaming sessions. Takes precedence over
// kCastStreamingForceEnableHardwareH264.
const char kCastStreamingForceDisableHardwareH264[] =
    "cast-streaming-force-disable-hardware-h264";

// If enabled, completely disables use of VP8 hardware encoding for Cast
// Streaming sessions. Takes precedence over
// kCastStreamingForceEnableHardwareVp8.
const char kCastStreamingForceDisableHardwareVp8[] =
    "cast-streaming-force-disable-hardware-vp8";

// If enabled, completely disables use of VP9 hardware encoding for Cast
// Streaming sessions. Takes precedence over
// kCastStreamingForceEnableHardwareVp9.
const char kCastStreamingForceDisableHardwareVp9[] =
    "cast-streaming-force-disable-hardware-vp9";

// If enabled, allows use of H264 hardware encoding for Cast Streaming sessions,
// even on platforms where it is disabled due to performance and reliability
// issues. kCastStreamingForceDisableHardwareH264 must be disabled for this flag
// to take effect.
const char kCastStreamingForceEnableHardwareH264[] =
    "cast-streaming-force-enable-hardware-h264";

// If enabled, allows use of VP8 hardware encoding for Cast Streaming sessions,
// even on platforms where it is disabled due to performance and reliability
// issues. kCastStreamingForceDisableHardwareVp8 must be disabled for this flag
// to take effect.
const char kCastStreamingForceEnableHardwareVp8[] =
    "cast-streaming-force-enable-hardware-vp8";

// If enabled, allows use of VP9 hardware encoding for Cast Streaming sessions,
// even on platforms where it is disabled due to performance and reliability
// issues. kCastStreamingForceDisableHardwareVp9 must be disabled for this flag
// to take effect.
const char kCastStreamingForceEnableHardwareVp9[] =
    "cast-streaming-force-enable-hardware-vp9";

// Specifies the path to the Clear Key CDM for testing, which is necessary to
// support External Clear Key key system when library CDM is enabled. Note that
// External Clear Key key system support is also controlled by feature
// kExternalClearKeyForTesting.
const char kClearKeyCdmPathForTesting[] = "clear-key-cdm-path-for-testing";

// Disable hardware acceleration of mjpeg decode for captured frame, where
// available.
const char kDisableAcceleratedMjpegDecode[] =
    "disable-accelerated-mjpeg-decode";

// Forces input and output stream creation to use fake audio streams.
const char kDisableAudioInput[] = "disable-audio-input";
const char kDisableAudioOutput[] = "disable-audio-output";

// Do not immediately suspend media in background tabs.
const char kDisableBackgroundMediaSuspend[] =
    "disable-background-media-suspend";

// Disables the new rendering algorithm for webrtc, which is designed to improve
// the rendering smoothness.
const char kDisableRTCSmoothnessAlgorithm[] =
    "disable-rtc-smoothness-algorithm";

// Sets the default value for the kLiveCaptionEnabled preference to true.
const char kEnableLiveCaptionPrefForTesting[] =
    "enable-live-caption-pref-for-testing";

// Causes the AudioManager to fail creating audio streams. Used when testing
// various failure cases.
const char kFailAudioStreamCreation[] = "fail-audio-stream-creation";

// Inserts fake background blur state into `VideoFrameMetadata`. The value
// represents the period in milliseconds. eg. Setting it to 1000ms, will cause
// the blur state to cycle between reporting ENABLED for 500ms and DISABLED for
// 500ms.
const char kFakeBackgroundBlurTogglePeriod[] =
    "fake-background-blur-toggle-period";

// Force media player using SurfaceView instead of SurfaceTexture on Android.
// Note: This is used by the Cast playback pipeline and must be kept.
const char kForceVideoOverlays[] = "force-video-overlays";

// Allows explicitly specifying MSE audio/video buffer sizes as megabytes.
// Default values are 150M for video and 12M for audio.
const char kMSEAudioBufferSizeLimitMb[] = "mse-audio-buffer-size-limit-mb";
const char kMSEVideoBufferSizeLimitMb[] = "mse-video-buffer-size-limit-mb";

// Mutes audio sent to the audio device so it is not audible during
// automated testing.
const char kMuteAudio[] = "mute-audio";

// Overrides the default enabled library CDM interface version(s) with the one
// specified with this switch, which will be the only version enabled. For
// example, on a build where CDM 8, CDM 9 and CDM 10 are all supported
// (implemented), but only CDM 8 and CDM 9 are enabled by default:
//  --override-enabled-cdm-interface-version=8 : Only CDM 8 is enabled
//  --override-enabled-cdm-interface-version=9 : Only CDM 9 is enabled
//  --override-enabled-cdm-interface-version=10 : Only CDM 10 is enabled
//  --override-enabled-cdm-interface-version=11 : No CDM interface is enabled
// This can be used for local testing and debugging. It can also be used to
// enable an experimental CDM interface (which is always disabled by default)
// for testing while it's still in development.
const char kOverrideEnabledCdmInterfaceVersion[] =
    "override-enabled-cdm-interface-version";

// Overrides hardware secure codecs support for testing. If specified, real
// platform hardware secure codecs check will be skipped. Valid codecs are:
// - video: "vp8", "vp9", "avc1", "hevc", "dolbyvision", "av01"
// - video that does not support clear lead: `<video>-no-clearlead`, where
//   <video> is from the list above.
// - audio: "mp4a", "vorbis"
// Codecs are separated by comma. For example:
//  --override-hardware-secure-codecs-for-testing=vp8,vp9-no-clearlead,vorbis
//  --override-hardware-secure-codecs-for-testing=avc1,mp4a
// CENC encryption scheme is assumed to be supported for the specified codecs.
// If no valid codecs specified, no hardware secure codecs are supported. This
// can be used to disable hardware secure codecs support:
//  --override-hardware-secure-codecs-for-testing
const char kOverrideHardwareSecureCodecsForTesting[] =
    "override-hardware-secure-codecs-for-testing";

// Force to report VP9 as an unsupported MIME type.
const char kReportVp9AsAnUnsupportedMimeType[] =
    "report-vp9-as-an-unsupported-mime-type";

// For automated testing of protected content, this switch allows specific
// domains (e.g. example.com) to always allow the permission to share the
// protected media identifier. In this context, domain does not include the
// port number. User's content settings will not be affected by enabling this
// switch.
// Reference: https://crbug.com/41317087
// Example:
// --unsafely-allow-protected-media-identifier-for-domain=a.com,b.ca
const char kUnsafelyAllowProtectedMediaIdentifierForDomain[] =
    "unsafely-allow-protected-media-identifier-for-domain";

// Use fake device for Media Stream to replace actual camera and microphone.
// For the list of allowed parameters, see
// FakeVideoCaptureDeviceFactory::ParseFakeDevicesConfigFromOptionsString().
const char kUseFakeDeviceForMediaStream[] = "use-fake-device-for-media-stream";

// Use a fake device for accelerated decoding of MJPEG. This allows, for
// example, testing of the communication to the GPU service without requiring
// actual accelerator hardware to be present.
const char kUseFakeMjpegDecodeAccelerator[] =
    "use-fake-mjpeg-decode-accelerator";

// Play a .wav file as the microphone. Note that for WebRTC calls we'll treat
// the bits as if they came from the microphone, which means you should disable
// audio processing (lest your audio file will play back distorted). The input
// file is converted to suit Chrome's audio buses if necessary, so most sane
// .wav files should work. You can pass either <path> to play the file looping
// or <path>%noloop to stop after playing the file to completion.
//
// Must also be used with kDisableAudioInput or kUseFakeDeviceForMediaStream.
const char kUseFileForFakeAudioCapture[] = "use-file-for-fake-audio-capture";

// Use an .y4m file to play as the webcam. See the comments in
// media/capture/video/file_video_capture_device.h for more details.
const char kUseFileForFakeVideoCapture[] = "use-file-for-fake-video-capture";

// Set number of threads to use for video decoding.
const char kVideoThreads[] = "video-threads";

#if BUILDFLAG(ENABLE_PASSTHROUGH_AUDIO_CODECS)
// Audio codecs supported by the HDMI sink is retrieved from the audio
// service process. EDID contains the Short Audio Descriptors, which list
// the audio decoders supported, and the information is presented as a
// bitmask of supported audio codecs.
const char kAudioCodecsFromEDID[] = "audio-codecs-from-edid";
#endif  // BUILDFLAG(ENABLE_PASSTHROUGH_AUDIO_CODECS)



#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_FREEBSD) || \
    BUILDFLAG(IS_SOLARIS)
// The Alsa device to use when opening an audio input stream.
const char kAlsaInputDevice[] = "alsa-input-device";
// The Alsa device to use when opening an audio stream.
const char kAlsaOutputDevice[] = "alsa-output-device";
#endif  // BUILDFLAG(IS_LINUX) || ...


#if BUILDFLAG(USE_CRAS)
// Enforce system audio echo cancellation.
const char kSystemAecEnabled[] = "system-aec-enabled";
// Use CRAS, the ChromeOS audio server.
const char kUseCras[] = "use-cras";
#endif  // BUILDFLAG(USE_CRAS)

#if BUILDFLAG(USE_V4L2_CODEC)
// This is needed for V4L2 testing using VISL (virtual driver) on cros VM with
// arm64-generic-vm. Minigbm buffer allocation is done using dumb driver with
// vkms.
const char kEnablePrimaryNodeAccessForVkmsTesting[] =
    "enable-primary-node-access-for-vkms-testing";

// Some (Qualcomm only at the moment) V4L2 video decoders require setting the
// framerate so that the hardware decoder can scale the clocks efficiently.
// This provides a mechanism during testing to lock the decoder framerate
// to a specific value.
const char kHardwareVideoDecodeFrameRate[] = "hardware-video-decode-framerate";
#endif  // BUILDFLAG(USE_V4L2_CODEC)

#if BUILDFLAG(USE_VAAPI)
// Allows explicitly picking a device path to find the device for VA-API video
// decoding and encoding.
const char kHardwareVideoDevicePath[] = "hardware-video-device-path";
#endif  // BUILDFLAG(USE_VAAPI)

}  // namespace switches

namespace media {

// Only used for disabling overlay fullscreen (aka SurfaceView) in Clank.
BASE_FEATURE(kOverlayFullscreenVideo,
             "overlay-fullscreen-video",
             base::FEATURE_ENABLED_BY_DEFAULT);

// We plan to remove the background pause timer feature from WebMediaPlayerImpl.
// We received reports that suggest that this feature's codepath hasn't been
// exercised for a long time. This is a finch killswitch to rollback to the
// previous behavior if we find any problems while disabling this feature.
BASE_FEATURE(kPauseBackgroundTimer, base::FEATURE_DISABLED_BY_DEFAULT);

// Enables tracking the position of picture-in-picture windows to know when they
// occlude certain widgets.
BASE_FEATURE(kPictureInPictureOcclusionTracking,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables the animation of the Picture-in-Picture window creation.
BASE_FEATURE(kPictureInPictureShowWindowAnimation,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables video Picture-in-Picture display smoothness optimization.
//
// Ensures that the video PiP window title view is properly sized to only fit
// the favicon and origin.
BASE_FEATURE(kVideoPipDisplaySmoothnessOptimization,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Forces video Picture-in-Picture windows to be treated as trusted for media
// playback. Used for debugging.
BASE_FEATURE(kVideoPipForceTrustedForMediaPlaybackForTesting,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enables tracking the occlusion of encrypted video elements.
BASE_FEATURE(kEncryptedMediaOcclusionTracking,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enables extended video bitstream validation for H.264 and H.265.
BASE_FEATURE(kExtendedVideoBitstreamValidation,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables user control over muting tab audio from the tab strip.
BASE_FEATURE(kEnableTabMuting, base::FEATURE_DISABLED_BY_DEFAULT);

#if BUILDFLAG(ENABLE_PLATFORM_HEVC)
// Enables HEVC hardware accelerated decoding.
BASE_FEATURE(kPlatformHEVCDecoderSupport, base::FEATURE_ENABLED_BY_DEFAULT);

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_ANDROID)
// Enables HEVC hardware accelerated encoding for Windows, Apple, and Android.
BASE_FEATURE(kPlatformHEVCEncoderSupport, base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_ANDROID)
#endif  // BUILDFLAG(ENABLE_PLATFORM_HEVC)

#if BUILDFLAG(ENABLE_HEVC_PARSER_AND_HW_DECODER)
// Enables HEVC MediaRecorder muxer support.
BASE_FEATURE(kMediaRecorderHEVCSupport, base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(ENABLE_HEVC_PARSER_AND_HW_DECODER)

// Let videos be resumed via remote controls (for example, the notification)
// when in background.
BASE_FEATURE(kResumeBackgroundVideo,
             "resume-background-video",
             base::FEATURE_DISABLED_BY_DEFAULT
);


#if BUILDFLAG(IS_MAC)
// Enables system audio loopback capture using the macOS CoreAudio tap API for
// Cast.
BASE_FEATURE(kMacCatapLoopbackAudioForCast, base::FEATURE_DISABLED_BY_DEFAULT);

// Enables system audio loopback capture using the macOS CoreAudio tap API for
// screen share.
BASE_FEATURE(kMacCatapLoopbackAudioForScreenShare,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Use the built-in MacOS screen-sharing picker (SCContentSharingPicker). This
// flag will only use the built-in picker on MacOS 15 Sequoia and later where it
// is required to avoid recurring permission dialogs.
BASE_FEATURE(kUseSCContentSharingPicker, base::FEATURE_DISABLED_BY_DEFAULT);

// Enables application audio capture for getDisplayMedia (gDM) window capture in
// macOS.
BASE_FEATURE(kApplicationAudioCaptureMac, base::FEATURE_DISABLED_BY_DEFAULT);

#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_LINUX)
// Enables system audio mirroring using pulseaudio.
BASE_FEATURE(kPulseaudioLoopbackForCast, base::FEATURE_DISABLED_BY_DEFAULT);

// Enables system audio sharing using pulseaudio.
BASE_FEATURE(kPulseaudioLoopbackForScreenShare,
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_LINUX)

// When enabled, MediaCapabilities will check with GPU Video Accelerator
// Factories to determine isPowerEfficient = true/false.
BASE_FEATURE(kMediaCapabilitiesQueryGpuFactories,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enable Media Capabilities with finch-parameters.
BASE_FEATURE(kMediaCapabilitiesWithParameters,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Used to set a few tunable parameters for the WebRTC Media Capabilities
// implementation.
BASE_FEATURE(kWebrtcMediaCapabilitiesParameters,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Controls the persistent license support for protected media that uses
// widevine.
BASE_FEATURE(kWidevinePersistentLicenseSupport,
#if BUILDFLAG(ENABLE_LIBRARY_CDMS)
             base::FEATURE_DISABLED_BY_DEFAULT);
#else
             base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(ENABLE_LIBRARY_CDMS)

// Use AndroidOverlay only if required for secure video playback. This requires
// that |kOverlayFullscreenVideo| is true, else it is ignored.
BASE_FEATURE(kUseAndroidOverlayForSecureOnly,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Allows usage of OS-level (platform) audio encoders.
BASE_FEATURE(kPlatformAudioEncoder,
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_ANDROID)
             base::FEATURE_ENABLED_BY_DEFAULT
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif
);

// CDM host verification is enabled by default. Can be disabled for testing.
// Has no effect if ENABLE_CDM_HOST_VERIFICATION buildflag is false.
BASE_FEATURE(kCdmHostVerification, base::FEATURE_ENABLED_BY_DEFAULT);

// Enables the "Copy Video Frame" context menu item.
BASE_FEATURE(kContextMenuCopyVideoFrame,
             base::FEATURE_ENABLED_BY_DEFAULT
);

// Enables the "Save Video Frame As" context menu item.
BASE_FEATURE(kContextMenuSaveVideoFrameAs,
             base::FEATURE_ENABLED_BY_DEFAULT
);

// Enables the "Search Video Frame with <Search Provider>" context menu item.
BASE_FEATURE(kContextMenuSearchForVideoFrame, base::FEATURE_ENABLED_BY_DEFAULT);

#if BUILDFLAG(CHROME_WIDE_ECHO_CANCELLATION)
// If echo cancellation for a mic signal is requested, mix and cancel all audio
// playback going to a specific output device in the audio service.
BASE_FEATURE(kChromeWideEchoCancellation, base::FEATURE_ENABLED_BY_DEFAULT);
#endif

#if BUILDFLAG(SYSTEM_LOOPBACK_AS_AEC_REFERENCE)
// If echo cancellation for a mic signal is requested, use system loopback
// audio as reference signal to be able to cancel echo from all audio processes
// and not only audio from Chrome.
BASE_FEATURE(kSystemLoopbackAsAecReference, base::FEATURE_ENABLED_BY_DEFAULT);
const base::FeatureParam<bool> kSystemLoopbackAsAecReferenceForcedOn{
    &kSystemLoopbackAsAecReference, "forced_on", false};
// If we are using system loopback as AEC reference, we delay the capture
// signal with `added_delay_ms` so that the reference signal arrives before
// the capture signal.
const base::FeatureParam<int> kAddedProcessingDelayMs{
    &kSystemLoopbackAsAecReference, "added_delay_ms", 170};
// Modifies the number of matched filters used in the AEC delay estimation when
// loopback system AEC is enabled.
const base::FeatureParam<int> kAecDelayNumFilters{
    &kSystemLoopbackAsAecReference, "num_filters", 6};
#endif

#if (BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN))
// Enforces the use of system echo cancellation.
BASE_FEATURE(kEnforceSystemEchoCancellation, base::FEATURE_DISABLED_BY_DEFAULT);

// If `EnforceSystemEchoCancellation` is enabled and echo cancellation (AEC) is
// requested, two additional parameters can be added: one for the AGC effect and
// one for the NS effect. Note that, system AGC/NS are never enabled
// independently of the system AEC.
//
// If true, system AGC/NS and the WebRTC AGC/NS will run in tandem
// (sequentially) if system echo cancellation is performed and a corresponding
// AGC/NS getUserMedia constraint is specified.
// Example on Windows: constraints ask for [AEC=true, NS=false, AGC=true]. If
// system AEC is supported, it will also lead to system NS and AGC independently
// of the supplied constraints since system effects can't be modified one by one
// on Windows. If `allow_agc_in_tandem` now is set to true, two AGC effects will
// therefore be enabled (system AGC and WebRTC AGC). Also, in this example,
// `allow_ns_in_tandem` will have no effect since setting it to true will not
// override the false constraint setting. Hence, WebRTC NS will be off (but
// system NS will still be on due to system echo cancellation running).
const base::FeatureParam<bool> kEnforceSystemEchoCancellationAllowAgcInTandem{
    &kEnforceSystemEchoCancellation, "allow_agc_in_tandem", false};
const base::FeatureParam<bool> kEnforceSystemEchoCancellationAllowNsInTandem{
    &kEnforceSystemEchoCancellation, "allow_ns_in_tandem", false};
#endif


// Controls whether the Mirroring Service will fetch, analyze, and store
// information on the quality of the session using RTCP logs.
BASE_FEATURE(kEnableRtcpReporting, base::FEATURE_ENABLED_BY_DEFAULT);

// Approach original pre-REC MSE object URL autorevoking behavior, though await
// actual attempt to use the object URL for attachment to perform revocation.
// This will hopefully reduce runtime memory bloat for pages that do not
// explicitly detach their HTMLME+MSE object collections nor explicitly revoke
// the object URLs used to attach HTMLME+MSE. When disabled, revocation only
// occurs when application explicitly revokes the object URL, or upon the
// execution context teardown for the MediaSource object. When enabled,
// revocation occurs upon successful start of attachment of HTMLME to the object
// URL. Note, rather than immediately scheduling a task to revoke upon the URL's
// creation, as at least one other browser does and the original File API
// pattern used to follow, this delay until attachment start enables new
// scenarios that could use the object URL for attaching HTMLME+MSE cross-thread
// (MSE-in-workers), where there could be significant delay between the worker
// thread creation of the object URL and the main thread usage of the object URL
// for starting attachment to HTMLME.
BASE_FEATURE(kRevokeMediaSourceObjectURLOnAttach,
             base::FEATURE_ENABLED_BY_DEFAULT);

#if BUILDFLAG(ENABLE_SYMPHONIA)
BASE_FEATURE(kSymphoniaAudioDecoding, base::FEATURE_DISABLED_BY_DEFAULT);
BASE_FEATURE(kSymphoniaMp3Decoding, base::FEATURE_DISABLED_BY_DEFAULT);
#endif

// Forces D3D11VideoDecoder to use one decoder texture per picture buffer.
// Owner: media-gpu-team@chromium.org
// Expiry: When no longer needed for decode texture selection experiments.
BASE_FEATURE(kD3D11VideoDecoderForceSingleTexture,
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kD3D11VideoDecoderUseSharedHandle,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Runs the media service in the GPU process on a dedicated thread.
BASE_FEATURE(kDedicatedMediaServiceThread,
             base::FEATURE_ENABLED_BY_DEFAULT
);

// Defer requesting persistent audio focus until the WebContents is audible.
// The goal is to prevent silent playback from taking audio focus from
// background apps on android, where focus is typically exclusive.
BASE_FEATURE(kDeferAudioFocusUntilAudible,
             base::FEATURE_DISABLED_BY_DEFAULT
);

// Allow document picture-in-picture to navigate.  This should be disabled
// except for testing.
BASE_FEATURE(kDocumentPictureInPictureNavigation,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Adds an animation to document picture-in-picture resizes.
BASE_FEATURE(kDocumentPictureInPictureAnimateResize,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Allows document picture-in-picture pages to request capture.
BASE_FEATURE(kDocumentPictureInPictureCapture,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Falls back to other decoders after audio/video decode error happens. The
// implementation may choose different strategies on when to fallback. See
// DecoderStream for details. When disabled, playback will fail immediately
// after a decode error happens. This can be useful in debugging and testing
// because the behavior is simpler and more predictable.
BASE_FEATURE(kFallbackAfterDecodeError, base::FEATURE_ENABLED_BY_DEFAULT);

// Blocks picture-in-picture windows while file dialogs are open.
BASE_FEATURE(kFileDialogsBlockPictureInPicture,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Tucks picture-in-picture windows while file dialogs are open.
BASE_FEATURE(kFileDialogsTuckPictureInPicture,
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
             base::FEATURE_ENABLED_BY_DEFAULT);
#else
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)

// Auto-dismiss global media controls.
BASE_FEATURE(kGlobalMediaControlsAutoDismiss, base::FEATURE_ENABLED_BY_DEFAULT);


// If enabled, users can request Media Remoting without fullscreen-in-tab.
BASE_FEATURE(kMediaRemotingWithoutFullscreen,
#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_LINUX)
             base::FEATURE_ENABLED_BY_DEFAULT
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif
);

// Enable selection of audio output device in Global Media Controls.
BASE_FEATURE(kGlobalMediaControlsSeamlessTransfer,
             base::FEATURE_DISABLED_BY_DEFAULT);

// CanPlayThrough issued according to standard.
BASE_FEATURE(kSpecCompliantCanPlayThrough, base::FEATURE_ENABLED_BY_DEFAULT);

// Suspends WebMediaPlayerImpl instances when the containing RenderFrame is
// frozen. TODO(crbug.com/41161335): Remove in M143 after it goes stable.
BASE_FEATURE(kSuspendMediaForFrozenFrames, base::FEATURE_ENABLED_BY_DEFAULT);

// Enables the Unified Autoplay policy by overriding the platform's default
// autoplay policy.
BASE_FEATURE(kUnifiedAutoplay, base::FEATURE_ENABLED_BY_DEFAULT);

#if BUILDFLAG(IS_LINUX)
// Enable vaapi/v4l2 video decoding on linux. This is already enabled by default
// on chromeos, but needs an experiment on linux.
BASE_FEATURE(kAcceleratedVideoDecodeLinux,
             "AcceleratedVideoDecoder",
#if BUILDFLAG(USE_VAAPI)
             base::FEATURE_ENABLED_BY_DEFAULT);
#else
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif

BASE_FEATURE(kAcceleratedVideoDecodeLinuxGL, base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kAcceleratedVideoEncodeLinux,
             "AcceleratedVideoEncoder",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Ignore the non-intel driver blacklist for VaapiVideoDecoder implementations.
// Intended for manual usage only in order to gague the status of newer driver
// implementations.
BASE_FEATURE(kVaapiIgnoreDriverChecks, base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_LINUX)

// NVIDIA VA-API drivers do not support Chromium and can sometimes cause
// crashes, disable VA-API on NVIDIA GPUs by default. See crbug.com/1492880.
BASE_FEATURE(kVaapiOnNvidiaGPUs, base::FEATURE_DISABLED_BY_DEFAULT);

// Enable VA-API hardware low power encoder for all codecs on intel Gen9x gpu.
BASE_FEATURE(kVaapiLowPowerEncoderGen9x, base::FEATURE_ENABLED_BY_DEFAULT);

// Ensure the advertised minimum supported resolution is larger than or equal to
// a given one (likely QVGA + 1) for certain codecs/modes and platforms, for
// performance reasons. This does not affect JPEG decoding.
//
// NOTE: This feature is default-enabled, but selectively disabled by tests
// so they can test resolutions below the threshold.  See crbug.com/40650027
BASE_FEATURE(kVaapiVideoMinResolutionForPerformance,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enable global VA-API lock. Disable this to use lock-free VA-API function
// calls for thread safe backends.
BASE_FEATURE(kGlobalVaapiLock, base::FEATURE_DISABLED_BY_DEFAULT);

#if defined(ARCH_CPU_X86_FAMILY) && BUILDFLAG(IS_CHROMEOS)
// Enable software bitrate controller for H264 temporal layer encoding with HW
// encoder on ChromeOS.
BASE_FEATURE(kVaapiH264SWBitrateController, base::FEATURE_DISABLED_BY_DEFAULT);
// Enable AV1 temporal layer encoding with HW encoder on ChromeOS.
BASE_FEATURE(kVaapiAV1TemporalLayerHWEncoding,
             "VaapiAv1TemporalLayerEncoding",
             base::FEATURE_ENABLED_BY_DEFAULT);
// Enable VP9 S-mode encoding with HW encoder for webrtc use case on ChromeOS.
BASE_FEATURE(kVaapiVp9SModeHWEncoding, base::FEATURE_ENABLED_BY_DEFAULT);
// Enables VSync aligned MJPEG decoding.
BASE_FEATURE(kVSyncMjpegDecoding, base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // defined(ARCH_CPU_X86_FAMILY) && BUILDFLAG(IS_CHROMEOS)
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)
// Enable H264 temporal layer encoding with V4L2 HW encoder on ChromeOS.
BASE_FEATURE(kV4L2H264TemporalLayerHWEncoding,
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif

// Inform video blitter of video color space.
BASE_FEATURE(kVideoBlitColorAccuracy,
             "video-blit-color-accuracy",
             base::FEATURE_ENABLED_BY_DEFAULT);

// A video encoder is allowed to drop a frame in cast mirroring.
BASE_FEATURE(kCastVideoEncoderFrameDrop, base::FEATURE_DISABLED_BY_DEFAULT);

// Enables optimizations to flush() and configure() in the WebCodecs' audio and
// video decoder implementations. Kill-switch to be removed after M145 stable.
BASE_FEATURE(kWebCodecsDecoderFlushOptimizations,
             base::FEATURE_ENABLED_BY_DEFAULT);

// A video encoder is allowed to drop a frame in WebCodecs.
BASE_FEATURE(kWebCodecsVideoEncoderFrameDrop,
             base::FEATURE_DISABLED_BY_DEFAULT);

// A hardware video encoder is allowed to drop a frame in WebRTC.
BASE_FEATURE(kWebRTCHardwareVideoEncoderFrameDrop,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Inform webrtc with correct video color space information whenever
// possible.
BASE_FEATURE(kWebRTCColorAccuracy, base::FEATURE_DISABLED_BY_DEFAULT);

// Enables support for External Clear Key (ECK) key system for testing on
// supported platforms. On platforms that do not support ECK, this feature has
// no effect.
BASE_FEATURE(kExternalClearKeyForTesting, base::FEATURE_DISABLED_BY_DEFAULT);


// Enables the On-Device Web Speech feature on supported devices.
BASE_FEATURE(kOnDeviceWebSpeech,
             base::FEATURE_ENABLED_BY_DEFAULT
);

// Enables on-device speech recognition using on-device Gemini Nano.
BASE_FEATURE(kOnDeviceWebSpeechGeminiNano, base::FEATURE_DISABLED_BY_DEFAULT);

// Enables the Live Caption feature on supported devices.
BASE_FEATURE(kLiveCaption, base::FEATURE_ENABLED_BY_DEFAULT);

// Logs a DumpWithoutCrashing() call each time the Speech On-Device API (SODA)
// fails to load. Used to diagnose issues when rolling out new versions of the
// SODA library.
BASE_FEATURE(kLogSodaLoadFailures,
             "kLogSodaLoadFailures",
             base::FEATURE_DISABLED_BY_DEFAULT);

// When getDisplayMedia() is invoked, the user sees and interacts with
// a Chromium prompt through which they choose which tab/window/screen
// to share. If this flag is enabled, then when the user chooses to
// share, transient activation is conferred on the capturing Web application.
//
// TODO(crbug.com/420406085): Remove after January 2028.
// Keep this flag around at least until that date.
BASE_FEATURE(kGetDisplayMediaConfersActivation,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enable the Speaker Change Detection feature, which inserts a line break when
// the Speech On-Device API (SODA) detects a speaker change.
BASE_FEATURE(kSpeakerChangeDetection, base::FEATURE_DISABLED_BY_DEFAULT);

// Log the amount of flickering between partial results. This measures how often
// the system revises earlier outputs, to quantify the system's output
// instability or flicker. Intuitively, it measures how many tokens must be
// truncated from the previous text before appending any new text. The erasure
// of the current timestep can be calculated from its longest common prefix with
// the previous timestep.
BASE_FEATURE(kLiveCaptionLogFlickerRate, base::FEATURE_DISABLED_BY_DEFAULT);

// Use a greedy text stabilizer to reduce flickering when translating partial
// speech recognition results.
BASE_FEATURE(kLiveCaptionUseGreedyTextStabilizer,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Use a wait-k approach to reduce flickering when translating partial speech
// recognition results.
BASE_FEATURE(kLiveCaptionUseWaitK, base::FEATURE_DISABLED_BY_DEFAULT);

// Enable experimental Live Caption languages.
BASE_FEATURE(kLiveCaptionExperimentalLanguages,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enable automatic downloading of speech recognition language packs.
BASE_FEATURE(kLiveCaptionAutomaticLanguageDownload,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enable or disable Live Caption from the right click menu.
BASE_FEATURE(kLiveCaptionRightClick, base::FEATURE_DISABLED_BY_DEFAULT);

// Enable or disable Live Caption support for WebAudio.
BASE_FEATURE(kLiveCaptionWebAudio, base::FEATURE_ENABLED_BY_DEFAULT);

// Prevents UrlProvisionFetcher from making a provisioning request. If
// specified, any provisioning request made will not be sent to the provisioning
// server, and the response will indicate a failure to communicate with the
// provisioning server.
BASE_FEATURE(kFailUrlProvisionFetcherForTesting,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enables hardware secure decryption if supported by hardware and CDM.
// NOTE: For Windows platform, hardware secure decryption is available via
// PlayReady SL3000.
// TODO(xhwang): Currently this is only used for development of new features.
// Apply this to Android and ChromeOS as well where hardware secure decryption
// is already available.
BASE_FEATURE(kHardwareSecureDecryption, base::FEATURE_ENABLED_BY_DEFAULT);

// By default, a codec is not supported for hardware secure decryption if it
// does not support clear lead. This option forces the support for testing.
const base::FeatureParam<bool> kHardwareSecureDecryptionForceSupportClearLead{
    &kHardwareSecureDecryption, "force_support_clear_lead", false};

// Same as `kHardwareSecureDecryption` above, but only enable experimental
// sub key systems. Which sub key system is experimental is key system specific.
BASE_FEATURE(kHardwareSecureDecryptionExperiment,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Allows automatically disabling hardware secure Content Decryption Module
// (CDM) after failures or crashes to fallback to software secure CDMs. If this
// feature is disabled, the fallback will never happen and users could be stuck
// in playback failures.
BASE_FEATURE(kHardwareSecureDecryptionFallback,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Whether disabling hardware secure Content Decryption Module
// (CDM) after failures or crashes to fallback to software secure CDMs should
// use per site logic.
const base::FeatureParam<bool> kHardwareSecureDecryptionFallbackPerSite{
    &kHardwareSecureDecryptionFallback, "per_site", true};

// The minimum and maximum number of days to disable hardware secure Content
// Decryption Module (CDM) as part of the fallback logic.
const base::FeatureParam<int> kHardwareSecureDecryptionFallbackMinDisablingDays{
    &kHardwareSecureDecryptionFallback, "min_disabling_days", 30};
const base::FeatureParam<int> kHardwareSecureDecryptionFallbackMaxDisablingDays{
    &kHardwareSecureDecryptionFallback, "max_disabling_days", 180};

// Whether selected HardwareContextReset events should be considered as failures
// in the hardware secure decryption fallback logic.
const base::FeatureParam<bool>
    kHardwareSecureDecryptionFallbackOnHardwareContextReset{
        &kHardwareSecureDecryptionFallback, "on_hardware_context_reset", true};

// Enables hardware secure AV1 decoding if supported by the hardware
// and the OS Content Decryption Module (CDM).
BASE_FEATURE(kHardwareSecureDecryptionAv1, base::FEATURE_ENABLED_BY_DEFAULT);

// Enables hardware secure VP9 decoding if supported by the hardware
// and the OS Content Decryption Module (CDM).
BASE_FEATURE(kHardwareSecureDecryptionVp9, base::FEATURE_DISABLED_BY_DEFAULT);


// Enables handling of hardware media keys for controlling media.
BASE_FEATURE(kHardwareMediaKeyHandling,
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
             base::FEATURE_ENABLED_BY_DEFAULT
#elif BUILDFLAG(IS_LINUX)
#if BUILDFLAG(USE_MPRIS)
             base::FEATURE_ENABLED_BY_DEFAULT
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif  // BUILDFLAG(USE_MPRIS)
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif  // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC) ||
        // BUILDFLAG(IS_LINUX)
);

// Enables a platform-specific resolution cutoff for prioritizing platform
// decoders over software decoders or vice-versa.
//
// Note: This feature is used by ChromeOS tests and shouldn't be removed even
// though it has long been enabled by default.
BASE_FEATURE(kResolutionBasedDecoderPriority, base::FEATURE_ENABLED_BY_DEFAULT);

// Allows the AutoPictureInPictureTabHelper to automatically enter
// picture-in-picture for websites with video playback (instead of only websites
// using camera or microphone).
BASE_FEATURE(kAutoPictureInPictureForVideoPlayback,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Enables Happiness Tracking Surveys for Auto Picture-in-Picture.
BASE_FEATURE(kAutoPictureInPictureSurveys, base::FEATURE_DISABLED_BY_DEFAULT);

// Enables showing auto picture-in-picture permission details in page info.
BASE_FEATURE(kAutoPictureInPicturePageInfoDetails,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Causes the AVC parser to additionally parse and indicate when an SEI
// recovery point with `recovery_frame_cnt=0` has been found.
BASE_FEATURE(kParseSEIRecoveryPoints, base::FEATURE_ENABLED_BY_DEFAULT);

// Allows media to autoplay without a user gesture if the site has been
// granted microphone or camera permissions.
BASE_FEATURE(kAutoplayBypassForMicCamera, base::FEATURE_DISABLED_BY_DEFAULT);

// Whether we should show a setting to disable autoplay policy.
BASE_FEATURE(kAutoplayDisableSettings, base::FEATURE_DISABLED_BY_DEFAULT);

// Whether we should allow color space changes to flush AcceleratedVideoDecoder.
BASE_FEATURE(kAVDColorSpaceChanges, base::FEATURE_ENABLED_BY_DEFAULT);

// Enables Browser Initiated Automatic Picture-in-Picture (ChAP) in dry run
// mode.
//
// When enabled in dry run mode, all ChAP code paths are executed with the
// exception of actually opening the video PiP window, or any other paths that
// may be visible to the user experience. This flag will be used to enable
// analyzing the feature impact and catch early any potential regressions.
BASE_FEATURE(kBrowserInitiatedAutomaticPictureInPictureDryRun,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Allows Chrome to reconfigure the sink to match the channel count of the
// source audio data. This ensures opening of an audio output stream to match
// the source audio data channels, to signal to the downstream audio
// subsystem that the audio must be processed according to the source audio
// channel count.
// TODO(crbug.com/445215599): This should be replaced with a MediaClient
// mechanism if it works as intended.
BASE_FEATURE(kMatchSourceAudioChannelLayout, base::FEATURE_DISABLED_BY_DEFAULT);


// TODO(crbug.com/414430336): Consider restricting to IS_CHROMEOS.
#if BUILDFLAG(USE_LINUX_VIDEO_ACCELERATION)
// Enable Variable Bitrate encoding with hardware accelerated encoders on
// ChromeOS.
BASE_FEATURE(kChromeOSHWVBREncoding,
#if defined(ARCH_CPU_X86_FAMILY)
             base::FEATURE_ENABLED_BY_DEFAULT
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif
);

// Limit the number of concurrent hardware decoder instances on ChromeOS.
BASE_FEATURE(kLimitConcurrentDecoderInstances,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Use SequencedTaskRunner for VideoEncodeAccelerator
BASE_FEATURE(kUseSequencedTaskRunnerForVEA,
             base::FEATURE_DISABLED_BY_DEFAULT);

#if defined(ARCH_CPU_ARM_FAMILY)
// Experimental support for GL based scaling for NV12 on Trogdor.
// Normally LibYUV is used to scale these frames. This flag enables
// an experimental GL-based scaling method.
// Owner: bchoobineh@chromium.org
// Expiry: When GLImageProcessor is deleted
BASE_FEATURE(kUseGLForScaling, base::FEATURE_ENABLED_BY_DEFAULT);
// Experimental support for GL based image processing. On some architectures,
// the hardware accelerated video decoder outputs frames in a format not
// understood by the display controller. We usually use LibYUV to convert these
// frames. This flag enables an experimental GL-based conversion method.
BASE_FEATURE(kPreferGLImageProcessor, base::FEATURE_DISABLED_BY_DEFAULT);
// Experimental support for software based MT21 conversion. On some (older)
// architectures, the hardware video decoder outputs frames in a pixel format
// known as MT21. Normally a hardware block performs to the conversion between
// this pixel format and NV12, but this flag will use a software equivalent
// instead.
BASE_FEATURE(kPreferSoftwareMT21, base::FEATURE_DISABLED_BY_DEFAULT);
// Enable populating the |needs_detiling| field in |VideoFrameMetadata|. This in
// turn triggers Skia to use the |VulkanImageProcessor| for detiling protected
// content.
// Owner: greenjustin@google.com
// Expiry: When Vulkan detiling is thoroughly tested and verified to work.
BASE_FEATURE(kEnableProtectedVulkanDetiling, base::FEATURE_ENABLED_BY_DEFAULT);
// Enable AR30 overlays for 10-bit ARM HWDRM content. If disabled, we will use
// ARGB8888 instead.
// Owner: greenjustin@google.com
// Expiry: When AR30 overlays are stable on devices that support them.
BASE_FEATURE(kEnableArmHwdrm10bitOverlays, base::FEATURE_ENABLED_BY_DEFAULT);
#if BUILDFLAG(USE_CHROMEOS_PROTECTED_MEDIA)
// Enable use of HW based L1 Widevine DRM via the cdm-oemcrypto daemon on
// ChromeOS. This flag is temporary while we finish development.
// Expiry: M133
// TODO(b/364969273): Remove this flag later.
BASE_FEATURE(kEnableArmHwdrm, base::FEATURE_ENABLED_BY_DEFAULT);
#endif  // BUILDFLAG(USE_CHROMEOS_PROTECTED_MEDIA)
#endif  // defined(ARCH_CPU_ARM_FAMILY)
#endif  // BUILDFLAG(USE_LINUX_VIDEO_ACCELERATION)

#if BUILDFLAG(ENABLE_OPENH264)
// Run-time feature for OpenH264 software encoder.
BASE_FEATURE(kOpenH264SoftwareEncoder,
             base::FEATURE_ENABLED_BY_DEFAULT
);
#endif  // BUILDFLAG(ENABLE_OPENH264)


#if BUILDFLAG(ENABLE_PLATFORM_ENCRYPTED_DOLBY_VISION)
// When ENABLE_PLATFORM_ENCRYPTED_DOLBY_VISION is enabled at build time, allow
// the support of encrypted Dolby Vision. Have no effect when
// ENABLE_PLATFORM_ENCRYPTED_DOLBY_VISION is disabled.
BASE_FEATURE(kPlatformEncryptedDolbyVision,
             base::FEATURE_DISABLED_BY_DEFAULT
);

// When ENABLE_PLATFORM_ENCRYPTED_DOLBY_VISION is enabled at build time and
// `kPlatformEncryptedDolbyVision` is enabled at run time, encrypted Dolby
// Vision is allowed in Media Source while clear Dolby Vision is not allowed.
// In this case, this feature allows the support of clear Dolby Vision in Media
// Source, which is useful to work around some JavaScript player limitations.
// Otherwise, this feature has no effect and neither encrypted nor clear Dolby
// Vision is allowed.
BASE_FEATURE(kAllowClearDolbyVisionInMseWhenPlatformEncryptedDvEnabled,
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif


#if BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
// Spawn utility processes to perform hardware decode acceleration on behalf of
// renderer processes (instead of using the GPU process). The GPU process will
// still be used as a proxy between renderers and utility processes (see
// go/oop-vd-dd).
BASE_FEATURE(kUseOutOfProcessVideoDecoding,
             base::FEATURE_DISABLED_BY_DEFAULT
);

// Use shared image interface to transport video frame resources.
// TODO(crbug.com/457296322): Enable after fixing issue where SharedImages are
// missing from the SharedImageManager.
BASE_FEATURE(kUseSharedImageInOOPVDProcess, base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
// Spawn utility processes to perform hardware encode acceleration instead of
// using the GPU process.
BASE_FEATURE(kUseOutOfProcessVideoEncoding, base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)

// Use SequencedTaskRunner for MediaService.
BASE_FEATURE(kUseSequencedTaskRunnerForMediaService,
             base::FEATURE_DISABLED_BY_DEFAULT);

// SequencedTaskRunner isn't supported on Windows since the accelerator requires
// a COM STA TaskRunner.
// Use SequencedTaskRunner for MojoVideoEncodeAcceleratorProvider.
BASE_FEATURE(kUseSequencedTaskRunnerForMojoVEAProvider,
#if BUILDFLAG(IS_APPLE)
             base::FEATURE_ENABLED_BY_DEFAULT
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif
);

// Feature flag to run the MojoAudioDecoderService in a sequence different than
// the other mojo media services. On some Android devices, MediaCodec may block
// the thread which leads to frequent audio decoder underrun in renderer.
// Running the audio decoder in a separate sequence can improve the performance.
// Note: running audio decoder in a separate thread/sequence will cause
// multithread access to mojo media services. For Android audio decoder, the
// thread safety is easier to guarantee because:
//   1. The audio decoder and most of the other mojo media services don't cross
//   reference each other.
//   2. The only exception is CDM so we use a lock inside MojoCdmServiceContext
//   for thread safety.
BASE_FEATURE(kUseTaskRunnerForMojoAudioDecoderService,
             base::FEATURE_DISABLED_BY_DEFAULT);

std::string GetEffectiveAutoplayPolicy(const base::CommandLine& command_line) {
  // Return the autoplay policy set in the command line, if any.
  if (command_line.HasSwitch(switches::kAutoplayPolicy)) {
    return command_line.GetSwitchValueASCII(switches::kAutoplayPolicy);
  }

  if (base::FeatureList::IsEnabled(media::kUnifiedAutoplay)) {
    return switches::autoplay::kDocumentUserActivationRequiredPolicy;
  }

// The default value is platform dependent.
  return switches::autoplay::kNoUserGestureRequiredPolicy;
}

// Enables Media Engagement Index recording. This data will be used to determine
// when to bypass autoplay policies. This is recorded on all platforms.
BASE_FEATURE(kRecordMediaEngagementScores, base::FEATURE_ENABLED_BY_DEFAULT);

// Enables Media Engagement Index recording for Web Audio playbacks.
BASE_FEATURE(kRecordWebAudioEngagement, base::FEATURE_ENABLED_BY_DEFAULT);

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)
// Reduces the number of buffers needed in the output video frame pool to
// populate the Renderer pipeline for hardware accelerated VideoDecoder in
// non-low latency scenarios.
BASE_FEATURE(kReduceHardwareVideoDecoderBuffers,
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif

// Enables the Enterprise Autoplay policies (AutoplayAllowed and
// AutoplayAllowlist) on Android.
BASE_FEATURE(kAutoplayPoliciesAndroid, base::FEATURE_DISABLED_BY_DEFAULT);

// The following Media Engagement flags are not enabled on mobile platforms:
// - MediaEngagementBypassAutoplayPolicies: enables the Media Engagement Index
//   data to be esude to override autoplay policies. An origin with a high MEI
//   will be allowed to autoplay.
// - PreloadMediaEngagementData: enables a list of origins to be considered as
//   having a high MEI until there is enough local data to determine the user's
//   preferred behaviour.
BASE_FEATURE(kMediaEngagementBypassAutoplayPolicies,
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
             base::FEATURE_DISABLED_BY_DEFAULT
#else
             base::FEATURE_ENABLED_BY_DEFAULT
#endif
);
BASE_FEATURE(kPreloadMediaEngagementData,
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
             base::FEATURE_DISABLED_BY_DEFAULT
#else
             base::FEATURE_ENABLED_BY_DEFAULT
#endif
);

BASE_FEATURE(kMediaEngagementHTTPSOnly, base::FEATURE_DISABLED_BY_DEFAULT);

// Enable the prototype global optimization of tuneables via finch.  See
// media/base/tuneable.h for how to create tuneable parameters.
BASE_FEATURE(kMediaOptimizer,
             "JointMediaOptimizer",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enable aggregate power measurement for media playback.
BASE_FEATURE(kMediaPowerExperiment, base::FEATURE_DISABLED_BY_DEFAULT);

// Enables audio ducking.
BASE_FEATURE(kAudioDucking, base::FEATURE_ENABLED_BY_DEFAULT);
// 0 = no attenuation
// 100 = fully muted
const base::FeatureParam<int> kAudioDuckingAttenuation{&kAudioDucking,
                                                       "attenuation", 80};


// Enables flash to be ducked by audio focus. This is enabled on Chrome OS which
// has audio focus enabled.
BASE_FEATURE(kAudioFocusDuckFlash,
             base::FEATURE_DISABLED_BY_DEFAULT
);

// Enables the internal Media Session logic without enabling the Media Session
// service.
BASE_FEATURE(kInternalMediaSession,
             base::FEATURE_DISABLED_BY_DEFAULT
);

BASE_FEATURE(kUseFakeDeviceForMediaStream,
             "use-fake-device-for-media-stream",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enables accurate dropped frame count for MediaStreamVideoSource.
// TODO(crbug.com/432367602): Remove after M143.
BASE_FEATURE(kMediaStreamAccurateDroppedFrameCount,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Controls whether mirroring negotiations will include the AV1 codec for video
// encoding.
//
// NOTE: currently only software AV1 encoding is supported.
// TODO(crbug.com/40246079): hardware AV1 encoding should be added.
BASE_FEATURE(kCastStreamingAv1, base::FEATURE_DISABLED_BY_DEFAULT);

// Controls whether the new exponential bitrate calculate logic is used, or
// the legacy linear algorithm.
// TODO(crbug.com/302584587): This is the V2 implementation of this experiment,
// following the failure of the initial experiment.
BASE_FEATURE(kCastStreamingExponentialVideoBitrateAlgorithm,
             base::FEATURE_DISABLED_BY_DEFAULT);
const base::FeatureParam<int>
    kCastStreamingExponentialVideoBitrateAlgorithmWindowSize{
        &kCastStreamingExponentialVideoBitrateAlgorithm, "window_size", 30};
const base::FeatureParam<int>
    kCastStreamingExponentialVideoBitrateAlgorithmDropThreshold{
        &kCastStreamingExponentialVideoBitrateAlgorithm, "drop_threshold", 1};
const base::FeatureParam<double>
    kCastStreamingExponentialVideoBitrateAlgorithmIncreaseFactor{
        &kCastStreamingExponentialVideoBitrateAlgorithm, "increase_factor",
        1.05};
const base::FeatureParam<double>
    kCastStreamingExponentialVideoBitrateAlgorithmDecreaseFactor{
        &kCastStreamingExponentialVideoBitrateAlgorithm, "decrease_factor",
        0.9};
const base::FeatureParam<double>
    kCastStreamingExponentialVideoBitrateAlgorithmDynamicWindowMultiplier{
        &kCastStreamingExponentialVideoBitrateAlgorithm,
        "dynamic_window_multiplier", 0.0};

BASE_FEATURE(kCastStreamingHardwareHevc, base::FEATURE_DISABLED_BY_DEFAULT);

// TODO(crbug.com/282984511): Remove after M151.
BASE_FEATURE(kCastStreamingMediaVideoEncoder,
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCastStreamingPerformanceOverlay,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Controls whether mirroring negotiations will include the VP9 codec for video
// encoding.
//
// NOTE: this is the default codec for Cast Streaming. Be careful when
// disabling.
BASE_FEATURE(kCastStreamingVp8, base::FEATURE_ENABLED_BY_DEFAULT);

// Controls whether mirroring negotiations will include the VP9 codec for video
// encoding.
BASE_FEATURE(kCastStreamingVp9, base::FEATURE_ENABLED_BY_DEFAULT);

#if BUILDFLAG(IS_MAC)
// Controls whether hardware H264 is default enabled on macOS.
BASE_FEATURE(kCastStreamingMacHardwareH264, base::FEATURE_ENABLED_BY_DEFAULT);
#endif



// Controls whether to pre-dispatch more decode tasks when pending decodes is
// smaller than maximum supported decodes as advertiszed by decoder.
// Note: This is controlled on a per-board basis by ChromeOS and must be kept.
BASE_FEATURE(kVideoDecodeBatching, base::FEATURE_ENABLED_BY_DEFAULT);

// Safety switch to allow us to revert to the previous behavior of using the
// cached bounds when the permission prompt is visible. If this feature is
// enabled (the default), we will clear the cached bounds, whenever the
// permission prompt is visible.
BASE_FEATURE(kClearPipCachedBoundsWhenPermissionPromptVisible,
             base::FEATURE_ENABLED_BY_DEFAULT);

// Safety switch to allow us to revert to the previous behavior of using the
// restored bounds for PiP windows, rather than the window bounds.  If this
// feature is enabled (the default), then we'll use the window bounds.
BASE_FEATURE(kUseWindowBoundsForPip, base::FEATURE_ENABLED_BY_DEFAULT);


// Enables sending MediaLog to the log stream, which is useful for easier
// development by ensuring logs can be seen without a remote desktop session.
// Only affects builds when DCHECK is on for non-ERROR logs (ERROR logs are
// always sent to the log stream). Enabled by default on Android and ChromeOS.
BASE_FEATURE(kMediaLogToConsole,
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_CHROMEOS)
             base::FEATURE_ENABLED_BY_DEFAULT
#else
             base::FEATURE_DISABLED_BY_DEFAULT
#endif
);

// Controls whether AOM/VPX decoders should use the presentation thread type.
BASE_FEATURE(kAomVpxUsePresentationThreadType,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enables the Rust-based JPEG parser.
BASE_FEATURE(kUseRustJpegParser, base::FEATURE_DISABLED_BY_DEFAULT);



// Controls whether muted media stream audio should continue to render.
BASE_FEATURE(kRenderMutedAudio, base::FEATURE_ENABLED_BY_DEFAULT);

// Controls whether audio is permitted to play if it's inaudible on a background
// tab. This is separate to the kRenderMutedAudio flag, which routes decoded
// audio into nowhere if the media is playing back. This flag instead _pauses_
// playback when the media goes to background to avoid wasting CPU power on
// decoding audio that cannot be heard. This flag will be switched on gradually
// via Finch.
BASE_FEATURE(kPauseMutedBackgroundAudio, base::FEATURE_DISABLED_BY_DEFAULT);


// Controls headless Live Caption experiment, which is likely unstable.
BASE_FEATURE(kHeadlessLiveCaption, base::FEATURE_DISABLED_BY_DEFAULT);

// If enabled, Glic will start captioning as soon as a profile is loaded.
BASE_FEATURE(kHeadlessCaptionEarlyStart, base::FEATURE_DISABLED_BY_DEFAULT);

// If enabled, chrome would inform Glic once it starts trasncribing, if Glic
// requested to be informed.
BASE_FEATURE(kMediaTrasncriptsFlagInPageMetadata,
             base::FEATURE_DISABLED_BY_DEFAULT);

// Allows per-site special processing for media links.
BASE_FEATURE(kMediaLinkHelpers, base::FEATURE_ENABLED_BY_DEFAULT);


bool IsChromeWideEchoCancellationEnabled() {
#if BUILDFLAG(CHROME_WIDE_ECHO_CANCELLATION)
  return base::FeatureList::IsEnabled(kChromeWideEchoCancellation) &&
         !IsSystemEchoCancellationEnforced();
#else
  return false;
#endif
}

BASE_FEATURE(kWebRtcAudioNeuralResidualEchoEstimation,
             base::FEATURE_DISABLED_BY_DEFAULT);

bool IsAudioProcessMlModelUsageEnabled() {
  if (!media::IsChromeWideEchoCancellationEnabled()) {
    // The feature relies on Chrome-wide echo cancellation being enabled,
    // because that is when the audio service has processing that may use a
    // model.
    return false;
  }
  return base::FeatureList::IsEnabled(kWebRtcAudioNeuralResidualEchoEstimation);
}

#if BUILDFLAG(IS_MAC)
namespace {
// Enables system audio loopback capture using the macOS Screen Capture Kit
// framework, regardless of the system version.
BASE_FEATURE(kMacSckSystemAudioLoopbackOverride,
             base::FEATURE_DISABLED_BY_DEFAULT);
}  // namespace

bool IsMacCatapSystemLoopbackCaptureSupported() {
  return (base::mac::MacOSVersion() >= 14'02'00);
}

bool IsMacSckSystemLoopbackCaptureSupported() {
  // Only supported on macOS 13.0+.
  // Disabled on macOS 15.0 due to problems with permission prompt.
  // The override feature is useful for testing on unsupported versions.
  return (base::mac::MacOSVersion() >= 13'00'00 &&
          base::mac::MacOSVersion() < 15'00'00) ||
         base::FeatureList::IsEnabled(kMacSckSystemAudioLoopbackOverride);
}
#endif  // BUILDFLAG(IS_MAC)


bool IsSystemLoopbackCaptureSupported() {
#if BUILDFLAG(IS_WIN) || BUILDFLAG(USE_CRAS)
  return true;
#elif BUILDFLAG(IS_MAC)
  return (IsMacSckSystemLoopbackCaptureSupported() ||
          IsMacCatapSystemLoopbackCaptureSupported());
#elif BUILDFLAG(IS_LINUX) && defined(USE_PULSEAUDIO)
  return true;
#else
  return false;
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(USE_CRAS)
}

bool IsApplicationLoopbackCaptureSupported() {
#if BUILDFLAG(IS_MAC)
  return base::FeatureList::IsEnabled(kApplicationAudioCaptureMac) &&
         base::FeatureList::IsEnabled(
             media::kMacCatapLoopbackAudioForScreenShare) &&
         media::IsMacCatapSystemLoopbackCaptureSupported();
#else
  return false;
#endif
}

bool IsSystemLoopbackAsAecReferenceEnabled() {
#if BUILDFLAG(SYSTEM_LOOPBACK_AS_AEC_REFERENCE)

#if BUILDFLAG(IS_MAC)
  if (!IsMacCatapSystemLoopbackCaptureSupported()) {
    return false;
  }
#endif
  return base::FeatureList::IsEnabled(kSystemLoopbackAsAecReference);

#else  // BUILDFLAG(SYSTEM_LOOPBACK_AS_AEC_REFERENCE)
  return false;
#endif
}

bool IsSystemLoopbackAsAecReferenceForcedOn() {
#if BUILDFLAG(SYSTEM_LOOPBACK_AS_AEC_REFERENCE)
  return IsSystemLoopbackAsAecReferenceEnabled() &&
         kSystemLoopbackAsAecReferenceForcedOn.Get();
#else
  return false;
#endif
}

#if BUILDFLAG(SYSTEM_LOOPBACK_AS_AEC_REFERENCE)
base::TimeDelta GetAecAddedDelay() {
  CHECK(IsSystemLoopbackAsAecReferenceEnabled());
  return base::Milliseconds(kAddedProcessingDelayMs.Get());
}

int GetAecDelayNumFilters() {
  CHECK(IsSystemLoopbackAsAecReferenceEnabled());
  return kAecDelayNumFilters.Get();
}
#endif  // BUILDFLAG(SYSTEM_LOOPBACK_AS_AEC_REFERENCE)

bool IsSystemEchoCancellationEnforced() {
#if (BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN))
  return base::FeatureList::IsEnabled(kEnforceSystemEchoCancellation);
#else
  return false;
#endif
}

bool IsSystemEchoCancellationEnforcedAndAllowNsInTandem() {
#if (BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN))
  return base::FeatureList::IsEnabled(media::kEnforceSystemEchoCancellation) &&
         media::kEnforceSystemEchoCancellationAllowNsInTandem.Get();
#else
  return false;
#endif
}

bool IsSystemEchoCancellationEnforcedAndAllowAgcInTandem() {
#if (BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN))
  return base::FeatureList::IsEnabled(media::kEnforceSystemEchoCancellation) &&
         media::kEnforceSystemEchoCancellationAllowAgcInTandem.Get();
#else
  return false;
#endif
}

bool IsDedicatedMediaServiceThreadEnabled(gl::ANGLEImplementation impl) {

  return base::FeatureList::IsEnabled(kDedicatedMediaServiceThread);
}

bool IsHardwareSecureDecryptionEnabled() {
  return base::FeatureList::IsEnabled(kHardwareSecureDecryption) ||
         base::FeatureList::IsEnabled(kHardwareSecureDecryptionExperiment);
}

bool IsVideoCaptureAcceleratedJpegDecodingEnabled() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableAcceleratedMjpegDecode)) {
    return false;
  }
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kUseFakeMjpegDecodeAccelerator)) {
    return true;
  }
  return false;
}

bool IsRestrictOwnAudioSupported() {
#if BUILDFLAG(IS_MAC)
  return IsMacCatapSystemLoopbackCaptureSupported() &&
         base::FeatureList::IsEnabled(kMacCatapLoopbackAudioForScreenShare);
#else
  return false;
#endif
}


#if BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
bool IsOutOfProcessVideoDecodingEnabled() {
  return base::FeatureList::IsEnabled(kUseOutOfProcessVideoDecoding);
}
#endif  // BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)

// Return bitmask of audio formats supported by EDID.
uint32_t GetPassthroughAudioFormats() {
#if BUILDFLAG(ENABLE_PASSTHROUGH_AUDIO_CODECS)
  // Return existing value if codec_bitmask has previously been retrieved,
  static const uint32_t codec_bitmask = []() {
    auto* command_line = base::CommandLine::ForCurrentProcess();
    uint32_t value = 0;
    if (command_line->HasSwitch(switches::kAudioCodecsFromEDID)) {
      const std::string switch_value =
          command_line->GetSwitchValueASCII(switches::kAudioCodecsFromEDID);
      if (!base::StringToUint(switch_value, &value)) {
        LOG(WARNING) << "Invalid value for --audio-codecs-from-edid: "
                     << switch_value << ". Falling back to 0.";
        return 0u;
      }
    }
    return value;
  }();
  return codec_bitmask;
#else
  return 0;
#endif  // BUILDFLAG(ENABLE_PASSTHROUGH_AUDIO_CODECS)
}

}  // namespace media
