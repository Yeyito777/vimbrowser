// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "base/allocator/partition_alloc_support.h"
#include "base/command_line.h"
#include "base/debug/alias.h"
#include "base/debug/leak_annotations.h"
#include "base/functional/bind.h"
#include "base/immediate_crash.h"
#include "base/message_loop/message_pump_type.h"
#include "base/metrics/histogram_functions.h"
#include "base/power_monitor/power_monitor.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/threading/hang_watcher.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "base/timer/hi_res_timer_manager.h"
#include "build/build_config.h"
#include "content/child/child_process.h"
#include "content/common/content_switches_internal.h"
#include "content/common/features.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/main_function_params.h"
#include "content/public/utility/content_utility_client.h"
#include "content/utility/on_device_model/on_device_model_sandbox_init.h"
#include "content/utility/utility_thread_impl.h"
#include "printing/buildflags/buildflags.h"
#include "sandbox/policy/mojom/sandbox.mojom.h"
#include "sandbox/policy/sandbox.h"
#include "sandbox/policy/sandbox_type.h"
#include "services/on_device_model/public/mojom/on_device_model_service.mojom.h"
#include "services/tracing/public/cpp/trace_startup.h"

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
#include "base/file_descriptor_store.h"
#include "base/files/file_util.h"
#include "base/pickle.h"
#include "content/child/sandboxed_process_thread_type_handler.h"
#include "content/common/gpu_pre_sandbox_hook_linux.h"
#include "content/public/common/content_descriptor_keys.h"
#include "content/utility/speech/speech_recognition_sandbox_hook_linux.h"
#include "media/gpu/buildflags.h"
#include "media/media_buildflags.h"
#include "sandbox/policy/linux/sandbox_linux.h"
#include "services/audio/audio_sandbox_hook_linux.h"
#include "services/network/network_sandbox_hook_linux.h"
#include "services/screen_ai/buildflags/buildflags.h"
#include "services/shape_detection/shape_detection_sandbox_hook.h"

#if BUILDFLAG(USE_LINUX_VIDEO_ACCELERATION)
#include "gpu/config/gpu_info_collector.h"
#include "media/gpu/sandbox/hardware_video_encoding_sandbox_hook_linux.h"
// gn check is not smart enough to realize that this include is guarded behind
// some BUILDFLAG()s and the BUILD.gn dependencies correctly account for that.
#include "third_party/angle/src/gpu_info_util/SystemInfo.h"  //nogncheck

#if BUILDFLAG(USE_VAAPI)
#include "media/gpu/vaapi/vaapi_wrapper.h"
#include "media/mojo/mojom/video_decoder_factory_process.mojom.h"
#endif  // BUILDFLAG(USE_VAAPI)

#endif  // BUILDFLAG(USE_LINUX_VIDEO_ACCELERATION)

#if BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
#include "media/gpu/sandbox/hardware_video_decoding_sandbox_hook_linux.h"
#endif  // BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)

#if BUILDFLAG(ENABLE_PRINTING)
#include "printing/sandbox/print_backend_sandbox_hook_linux.h"
#endif

#if BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)
#include "services/screen_ai/public/cpp/utilities.h"  // nogncheck
#include "services/screen_ai/sandbox/screen_ai_sandbox_hook_linux.h"  // nogncheck
#endif

#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)


#if BUILDFLAG(IS_MAC)
#include "base/message_loop/message_pump_apple.h"
#endif



        // BUILDFLAG(IS_CHROMEOS))

namespace content {

namespace {

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
std::vector<std::string> GetNetworkContextsParentDirectories() {
  base::MemoryMappedFile::Region region;
  base::ScopedFD read_pipe_fd = base::FileDescriptorStore::GetInstance().TakeFD(
      kNetworkContextParentDirsDescriptor, &region);
  DCHECK(region == base::MemoryMappedFile::Region::kWholeFile);

  std::string dirs_str;
  if (!base::ReadStreamToString(fdopen(read_pipe_fd.get(), "r"), &dirs_str)) {
    LOG(FATAL) << "Failed to read network context parents dirs from pipe.";
  }

  base::PickleIterator dirs_pickle_iter =
      base::PickleIterator::WithData(base::as_byte_span(dirs_str));

  std::vector<std::string> dirs;
  std::string dir;
  while (dirs_pickle_iter.ReadString(&dir)) {
    dirs.push_back(dir);
  }

  CHECK(dirs_pickle_iter.ReachedEnd());

  return dirs;
}

bool ShouldUseAmdGpuPolicy(sandbox::mojom::Sandbox sandbox_type) {
#if BUILDFLAG(USE_LINUX_VIDEO_ACCELERATION) || \
    BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
  const bool obtain_gpu_info =
      sandbox_type == sandbox::mojom::Sandbox::kHardwareVideoDecoding ||
      sandbox_type == sandbox::mojom::Sandbox::kHardwareVideoEncoding;

  if (obtain_gpu_info) {
    // The kHardwareVideoDecoding and kHardwareVideoEncoding sandboxes need to
    // know the GPU type in order to select the right policy.
    gpu::GPUInfo gpu_info{};
    gpu::CollectBasicGraphicsInfo(&gpu_info);
    return angle::IsAMD(gpu_info.active_gpu().vendor_id);
  }
#endif  // BUILDFLAG(USE_LINUX_VIDEO_ACCELERATION) ||
        // BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
  return false;
}
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)


void SetUtilityThreadName(const std::string& utility_sub_type) {
  // Typical utility sub-types are audio.mojom.AudioService or
  // proxy_resolver.mojom.ProxyResolverFactory. Using the full sub-type as part
  // of the thread name is too verbose so we take the text in front of the first
  // period and use that as a prefix. This give us thread names like
  // audio.CrUtilityMain and proxy_resolver.CrUtilityMain. If there is no period
  // then the entire utility_sub_type string will be put in front.
  auto first_period = utility_sub_type.find('.');
  base::PlatformThread::SetName(utility_sub_type.substr(0, first_period) +
                                ".CrUtilityMain");
}

}  // namespace

// Mainline routine for running as the utility process.
int UtilityMain(MainFunctionParams parameters) {
  if (parameters.command_line->HasSwitch(
          switches::kUtilityImmediateCrashForTesting)) {
    base::ImmediateCrash();
  }

  base::MessagePumpType message_pump_type =
      parameters.command_line->HasSwitch(switches::kMessageLoopTypeUi)
          ? base::MessagePumpType::UI
          : base::MessagePumpType::DEFAULT;

#if BUILDFLAG(IS_MAC)
  auto sandbox_type =
      sandbox::policy::SandboxTypeFromCommandLine(*parameters.command_line);
  if (sandbox_type != sandbox::mojom::Sandbox::kNoSandbox) {
    // On Mac, the TYPE_UI pump for the main thread is an NSApplication loop.
    // In a sandboxed utility process, NSApp attempts to acquire more Mach
    // resources than a restrictive sandbox policy should allow. Services that
    // require a TYPE_UI pump generally just need a NS/CFRunLoop to pump system
    // work sources, so choose that pump type instead. A NSRunLoop MessagePump
    // is used for TYPE_UI MessageLoops on non-main threads.
    base::MessagePump::OverrideMessagePumpForUIFactory(
        []() -> std::unique_ptr<base::MessagePump> {
          return std::make_unique<base::MessagePumpNSRunLoop>();
        });
  }
#endif


  // The main task executor of the utility process.
  base::SingleThreadTaskExecutor main_thread_task_executor(
      message_pump_type, /*is_main_thread=*/true);
  const std::string utility_sub_type =
      parameters.command_line->GetSwitchValueASCII(switches::kUtilitySubType);
  SetUtilityThreadName(utility_sub_type);

  if (parameters.command_line->HasSwitch(switches::kUtilityStartupDialog)) {
    auto dialog_match = parameters.command_line->GetSwitchValueASCII(
        switches::kUtilityStartupDialog);
    if (dialog_match.empty() || dialog_match == utility_sub_type) {
      WaitForDebugger(utility_sub_type.empty() ? "Utility" : utility_sub_type);
    }
  }

  if (utility_sub_type == on_device_model::mojom::OnDeviceModelService::Name_) {
    CHECK(on_device_model::PreSandboxInit());
  }

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(USE_LINUX_VIDEO_ACCELERATION) && BUILDFLAG(USE_VAAPI)
  // Regardless of the sandbox status, the VaapiWrapper needs to be initialized
  // for decoder utility processes on devices that use VA-API.
  if (utility_sub_type == media::mojom::VideoDecoderFactoryProcess::Name_) {
    media::VaapiWrapper::PreSandboxInitialization(
        /*allow_disabling_global_lock=*/true);
  }
#endif  // BUILDFLAG(USE_LINUX_VIDEO_ACCELERATION) && BUILDFLAG(USE_VAAPI)

  // Thread type delegate of the process should be registered before first
  // thread type change in ChildProcess constructor. It also needs to be
  // registered before the process has multiple threads, which may race with
  // application of the sandbox.
  SandboxedProcessThreadTypeHandler::Create();

  // Initializes the sandbox before any threads are created.
  // TODO(jorgelo): move this after GTK initialization when we enable a strict
  // Seccomp-BPF policy.
  auto sandbox_type =
      sandbox::policy::SandboxTypeFromCommandLine(*parameters.command_line);
  sandbox::policy::SandboxLinux::Options sandbox_options;
  sandbox::policy::SandboxLinux::PreSandboxHook pre_sandbox_hook;
  switch (sandbox_type) {
    case sandbox::mojom::Sandbox::kNetwork:
      pre_sandbox_hook = base::BindOnce(&network::NetworkPreSandboxHook,
                                        GetNetworkContextsParentDirectories());
      break;
    case sandbox::mojom::Sandbox::kPrintBackend:
#if BUILDFLAG(ENABLE_OOP_PRINTING)
      pre_sandbox_hook = base::BindOnce(&printing::PrintBackendPreSandboxHook);
      break;
#else
      NOTREACHED();
#endif  // BUILDFLAG(ENABLE_OOP_PRINTING)
    case sandbox::mojom::Sandbox::kAudio:
      pre_sandbox_hook = base::BindOnce(&audio::AudioPreSandboxHook);
      break;
    case sandbox::mojom::Sandbox::kOnDeviceModelExecution:
      on_device_model::AddSandboxLinuxOptions(sandbox_options);
      pre_sandbox_hook = base::BindOnce(&on_device_model::PreSandboxHook);
      break;
    case sandbox::mojom::Sandbox::kSpeechRecognition:
      pre_sandbox_hook =
          base::BindOnce(&speech::SpeechRecognitionPreSandboxHook);
      break;
        // BUILDFLAG(IS_CHROMEOS))
    case sandbox::mojom::Sandbox::kScreenAI:
#if BUILDFLAG(ENABLE_SCREEN_AI_SERVICE)
      pre_sandbox_hook =
          base::BindOnce(&screen_ai::ScreenAIPreSandboxHook,
                         parameters.command_line->GetSwitchValuePath(
                             screen_ai::GetBinaryPathSwitch()));
      break;
#else
      NOTREACHED();
#endif
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    case sandbox::mojom::Sandbox::kShapeDetection:
      pre_sandbox_hook =
          base::BindOnce(&shape_detection::ShapeDetectionPreSandboxHook);
      break;
#if BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
    case sandbox::mojom::Sandbox::kHardwareVideoDecoding:
      pre_sandbox_hook =
          base::BindOnce(&media::HardwareVideoDecodingPreSandboxHook);
      break;
#endif  // BUILDFLAG(ALLOW_OOP_VIDEO_DECODER)
#if BUILDFLAG(USE_LINUX_VIDEO_ACCELERATION)
    case sandbox::mojom::Sandbox::kHardwareVideoEncoding:
      pre_sandbox_hook =
          base::BindOnce(&media::HardwareVideoEncodingPreSandboxHook);
      break;
#endif  // BUILDFLAG(USE_LINUX_VIDEO_ACCELERATION)
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
    default:
      break;
  }
  if (!sandbox::policy::IsUnsandboxedSandboxType(sandbox_type) &&
      (parameters.zygote_child || !pre_sandbox_hook.is_null())) {
    sandbox_options.use_amd_specific_policies =
        ShouldUseAmdGpuPolicy(sandbox_type);
    sandbox::policy::Sandbox::Initialize(
        sandbox_type, std::move(pre_sandbox_hook), sandbox_options);
  }

  // Startup tracing creates a tracing thread, which is incompatible on
  // platforms that require single-threaded sandbox initialization. In these
  // cases, startup tracing is initialized right after sandbox initialization.
  if (parameters.needs_startup_tracing_after_sandbox_init) {
    tracing::InitTracingPostFeatureList(/*enable_consumer=*/false,
                                        /*will_trace_thread_restart=*/false);
  }

  // Start the HangWatcher now that the sandbox is engaged, if it hasn't
  // already been started.
  if (base::HangWatcher::IsEnabled() &&
      !base::HangWatcher::GetInstance()->IsStarted()) {
    DCHECK(parameters.hang_watcher_not_started_time.has_value());
    base::TimeDelta uncovered_hang_watcher_time =
        base::TimeTicks::Now() -
        parameters.hang_watcher_not_started_time.value();
    base::UmaHistogramTimes("HangWatcher.UtilityProcess.UncoveredStartupTime",
                            uncovered_hang_watcher_time);
    base::HangWatcher::GetInstance()->Start();
  }

#endif

  ChildProcess utility_process(base::ThreadType::kDefault);
  GetContentClient()->utility()->PostIOThreadCreated(
      utility_process.io_task_runner());
  base::RunLoop run_loop;
  utility_process.set_main_thread(
      new UtilityThreadImpl(run_loop.QuitClosure()));

  // Both utility process and service utility process would come
  // here, but the later is launched without connection to service manager, so
  // there has no base::PowerMonitor be created(See ChildThreadImpl::Init()).
  // As base::PowerMonitor is necessary to base::HighResolutionTimerManager, for
  // such case we just disable base::HighResolutionTimerManager for now.
  // Note that disabling base::HighResolutionTimerManager means high resolution
  // timer is always disabled no matter on battery or not, but it should have
  // no any bad influence because currently service utility process is not using
  // any high resolution timer.
  // TODO(leonhsl): Once http://crbug.com/646833 got resolved, re-enable
  // base::HighResolutionTimerManager here for future possible usage of high
  // resolution timer in service utility process.
  std::optional<base::HighResolutionTimerManager> hi_res_timer_manager;
  if (base::PowerMonitor::GetInstance()->IsInitialized()) {
    hi_res_timer_manager.emplace();
  }

  base::allocator::PartitionAllocSupport::Get()->ReconfigureAfterTaskRunnerInit(
      switches::kUtilityProcess);

  run_loop.Run();

  if (utility_sub_type == on_device_model::mojom::OnDeviceModelService::Name_) {
    CHECK(on_device_model::Shutdown());
  }

#if defined(LEAK_SANITIZER)
  // Invoke LeakSanitizer before shutting down the utility thread, to avoid
  // reporting shutdown-only leaks.
  __lsan_do_leak_check();
#endif

  return 0;
}

}  // namespace content
