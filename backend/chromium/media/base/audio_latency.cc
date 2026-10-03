// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/audio_latency.h"

#include <stdint.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <numeric>

#include "base/check_op.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "media/base/limits.h"
#include "media/media_buildflags.h"



namespace media {

// static
bool AudioLatency::IsResamplingPassthroughSupported(Type type) {
  return false;
}

// static
int AudioLatency::GetHighLatencyBufferSize(int sample_rate,
                                           int preferred_buffer_size) {
#if BUILDFLAG(USE_CRAS)
  // Use 80ms rounded to a power of 2.
  const double eighty_ms_size = 8.0 * sample_rate / 100;
  const int high_latency_buffer_size =
      std::bit_ceil(static_cast<uint32_t>(std::round(eighty_ms_size)));
#else
  // On other platforms use the nearest higher power of two buffer size.  For a
  // given sample rate, this works out to:
  //
  //     <= 3200   : 64
  //     <= 6400   : 128
  //     <= 12800  : 256
  //     <= 25600  : 512
  //     <= 51200  : 1024
  //     <= 102400 : 2048
  //     <= 204800 : 4096
  //
  // On Linux, the minimum hardware buffer size is 512, so the lower calculated
  // values are unused.  OSX may have a value as low as 128.
  const double twenty_ms_size = 2.0 * sample_rate / 100;
  const int high_latency_buffer_size =
      std::bit_ceil(static_cast<uint32_t>(std::round(twenty_ms_size)));
#endif

  return std::max(preferred_buffer_size, high_latency_buffer_size);
}

// static
int AudioLatency::GetRtcBufferSize(int sample_rate, int hardware_buffer_size) {
  // Use native hardware buffer size as default. On Windows, we strive to open
  // up using this native hardware buffer size to achieve best
  // possible performance and to ensure that no FIFO is needed on the browser
  // side to match the client request. That is why there is no #if case for
  // Windows below.
  int frames_per_buffer = hardware_buffer_size;

  // No |hardware_buffer_size| is specified, fall back to 10 ms buffer size.
  if (!frames_per_buffer) {
    frames_per_buffer = sample_rate / 100;
    DVLOG(1) << "Using 10 ms sink output buffer size: " << frames_per_buffer;
    return frames_per_buffer;
  }

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_APPLE) || BUILDFLAG(IS_FUCHSIA)
  // On Linux, MacOS and Fuchsia, the low level IO implementations on the
  // browser side supports all buffer size the clients want. We use the native
  // peer connection buffer size (10ms) to achieve best possible performance.
  frames_per_buffer = sample_rate / 100;
#endif

  DVLOG(1) << "Using sink output buffer size: " << frames_per_buffer;
  return frames_per_buffer;
}

// static
int AudioLatency::GetInteractiveBufferSize(int hardware_buffer_size) {
  CHECK_GT(hardware_buffer_size, 0);

  return hardware_buffer_size;
}

int AudioLatency::GetExactBufferSize(base::TimeDelta duration,
                                     int sample_rate,
                                     int hardware_buffer_size,
                                     int min_hardware_buffer_size,
                                     int max_hardware_buffer_size,
                                     int max_allowed_buffer_size) {
  DCHECK_NE(0, hardware_buffer_size);
  DCHECK_NE(0, max_allowed_buffer_size);
  DCHECK_GE(hardware_buffer_size, min_hardware_buffer_size);
  DCHECK_GE(max_hardware_buffer_size, min_hardware_buffer_size);
  DCHECK(max_hardware_buffer_size == 0 ||
         hardware_buffer_size <= max_hardware_buffer_size);
  DCHECK_LE(hardware_buffer_size, max_allowed_buffer_size);

  int requested_buffer_size = std::round(duration.InSecondsF() * sample_rate);

  if (min_hardware_buffer_size &&
      requested_buffer_size <= min_hardware_buffer_size) {
    return min_hardware_buffer_size;
  }

  if (requested_buffer_size <= hardware_buffer_size)
    return hardware_buffer_size;

  const int multiplier = min_hardware_buffer_size > 0 ? min_hardware_buffer_size
                                                      : hardware_buffer_size;

  int buffer_size =
      std::ceil(requested_buffer_size / static_cast<double>(multiplier)) *
      multiplier;

  // If the user is requesting a buffer size >= max_hardware_buffer_size then we
  // want the hardware to run at this max and then only return sizes that are
  // multiples of this here so that we don't end up with Web Audio running with
  // a period that's misaligned with the hardware one.
  if (max_hardware_buffer_size && buffer_size >= max_hardware_buffer_size) {
    buffer_size = std::ceil(requested_buffer_size /
                            static_cast<double>(max_hardware_buffer_size)) *
                  max_hardware_buffer_size;
  }

  const int platform_max_buffer_size =
      (max_hardware_buffer_size &&
       max_hardware_buffer_size <= max_allowed_buffer_size)
          ? (max_allowed_buffer_size / max_hardware_buffer_size) *
                max_hardware_buffer_size
          : (max_allowed_buffer_size / multiplier) * multiplier;

  return std::min(buffer_size, platform_max_buffer_size);
}

// static
// Used for UMA histogram names, do not change the lookup.
const char* AudioLatency::ToString(Type type) {
  switch (type) {
    case Type::kExactMS:
      return "LatencyExactMs";
    case Type::kInteractive:
      return "LatencyInteractive";
    case Type::kRtc:
      return "LatencyRtc";
    case Type::kPlayback:
      return "LatencyPlayback";
    case Type::kUnknown:
      return "LatencyUnknown";
  }
}
}  // namespace media
