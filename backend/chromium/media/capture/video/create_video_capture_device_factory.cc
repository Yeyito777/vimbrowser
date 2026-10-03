// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/create_video_capture_device_factory.h"

#include "base/command_line.h"
#include "base/notimplemented.h"
#include "base/system/sys_info.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "gpu/config/gpu_driver_bug_workarounds.h"
#include "media/base/media_switches.h"
#include "media/capture/video/fake_video_capture_device_factory.h"
#include "media/capture/video/file_video_capture_device_factory.h"

#if BUILDFLAG(IS_LINUX)
#include "media/capture/video/linux/video_capture_device_factory_linux.h"
#elif BUILDFLAG(IS_APPLE)
#include "media/capture/video/apple/video_capture_device_factory_apple.h"
#endif

namespace media {

namespace {

std::unique_ptr<VideoCaptureDeviceFactory>
CreateFakeVideoCaptureDeviceFactory() {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  // Use a File Video Device Factory if the command line flag is present.
  // Otherwise, use a Fake Video Device Factory.
  if (command_line->HasSwitch(switches::kUseFileForFakeVideoCapture)) {
    return std::make_unique<FileVideoCaptureDeviceFactory>();
  } else {
    std::vector<FakeVideoCaptureDeviceSettings> config;
    FakeVideoCaptureDeviceFactory::ParseFakeDevicesConfigFromOptionsString(
        command_line->GetSwitchValueASCII(
            switches::kUseFakeDeviceForMediaStream),
        &config);
    auto result = std::make_unique<FakeVideoCaptureDeviceFactory>();
    result->SetToCustomDevicesConfig(config);
    return std::move(result);
  }
}

std::unique_ptr<VideoCaptureDeviceFactory>
CreatePlatformSpecificVideoCaptureDeviceFactory(
    scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner,
    gpu::GpuDriverBugWorkarounds* gpu_workarounds) {
#if BUILDFLAG(IS_LINUX)
  return std::make_unique<VideoCaptureDeviceFactoryLinux>(ui_task_runner);
#elif BUILDFLAG(IS_APPLE)
  return std::make_unique<VideoCaptureDeviceFactoryApple>();
#else
  NOTIMPLEMENTED();
  return nullptr;
#endif
}

}  // anonymous namespace

bool ShouldUseFakeVideoCaptureDeviceFactory() {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  return command_line->HasSwitch(switches::kUseFakeDeviceForMediaStream);
}

std::unique_ptr<VideoCaptureDeviceFactory> CreateVideoCaptureDeviceFactory(
    scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner,
    gpu::GpuDriverBugWorkarounds* gpu_workarounds) {
  if (ShouldUseFakeVideoCaptureDeviceFactory()) {
    return CreateFakeVideoCaptureDeviceFactory();
  } else {
    // |ui_task_runner| is needed for the Linux ChromeOS factory to retrieve
    // screen rotations.
    return CreatePlatformSpecificVideoCaptureDeviceFactory(ui_task_runner,
                                                           gpu_workarounds);
  }
}

}  // namespace media
