// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_metrics.h"

#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/byte_size.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/cpu.h"
#include "base/files/dir_reader_posix.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/memory/page_size.h"
#include "base/memory/ptr_util.h"
#include "base/notreached.h"
#include "base/numerics/clamped_math.h"
#include "base/numerics/safe_conversions.h"
#include "base/process/internal_linux.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/string_util.h"
#include "base/system/sys_info.h"
#include "base/threading/thread_restrictions.h"
#include "base/trace_event/trace_event.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "build/build_config.h"
#include "third_party/abseil-cpp/absl/strings/ascii.h"

namespace base {

class ScopedAllowBlockingForProcessMetrics : public ScopedAllowBlocking {};

namespace {


// Get the total CPU from a proc stat buffer. Return value is a TimeDelta
// converted from a number of jiffies on success or an error code if parsing
// failed.
base::expected<TimeDelta, ProcessCPUUsageError> ParseTotalCPUTimeFromStats(
    base::span<std::string_view> proc_stats) {
  const std::optional<int64_t> utime =
      internal::GetProcStatsFieldAsOptionalInt64(proc_stats,
                                                 internal::VM_UTIME);
  if (utime.value_or(-1) < 0) {
    return base::unexpected(ProcessCPUUsageError::kSystemError);
  }
  const std::optional<int64_t> stime =
      internal::GetProcStatsFieldAsOptionalInt64(proc_stats,
                                                 internal::VM_STIME);
  if (stime.value_or(-1) < 0) {
    return base::unexpected(ProcessCPUUsageError::kSystemError);
  }
  const TimeDelta cpu_time = internal::ClockTicksToTimeDelta(
      base::ClampAdd(utime.value(), stime.value()));
  CHECK(!cpu_time.is_negative());
  return base::ok(cpu_time);
}

size_t GetKbFieldAsSizeT(std::string_view value_str) {
  std::vector<std::string_view> split_value_str =
      SplitStringPiece(value_str, " ", TRIM_WHITESPACE, SPLIT_WANT_ALL);
  CHECK(split_value_str.size() == 2 && split_value_str[1] == "kB");
  size_t value;
  CHECK(StringToSizeT(split_value_str[0], &value));
  return value;
}

}  // namespace

// static
std::unique_ptr<ProcessMetrics> ProcessMetrics::CreateProcessMetrics(
    ProcessHandle process) {
  return WrapUnique(new ProcessMetrics(process));
}

base::expected<TimeDelta, ProcessCPUUsageError>
ProcessMetrics::GetCumulativeCPUUsage() {
  TRACE_EVENT("base", "GetCumulativeCPUUsage");
  std::string buffer;
  std::vector<std::string_view> proc_stats;
  if (!internal::ReadProcStats(process_, &buffer) ||
      !internal::ParseProcStats(buffer, &proc_stats)) {
    return base::unexpected(ProcessCPUUsageError::kSystemError);
  }

  return ParseTotalCPUTimeFromStats(proc_stats);
}

bool ProcessMetrics::GetCumulativeCPUUsagePerThread(
    CPUUsagePerThread& cpu_per_thread) {
  cpu_per_thread.clear();

  internal::ForEachProcessTask(
      process_,
      [&cpu_per_thread](PlatformThreadId tid, const FilePath& task_path) {
        FilePath thread_stat_path = task_path.Append("stat");

        std::string buffer;
        std::vector<std::string_view> proc_stats;
        if (!internal::ReadProcFile(thread_stat_path, &buffer) ||
            !internal::ParseProcStats(buffer, &proc_stats)) {
          return;
        }

        const base::expected<TimeDelta, ProcessCPUUsageError> thread_time =
            ParseTotalCPUTimeFromStats(proc_stats);
        if (thread_time.has_value()) {
          cpu_per_thread.emplace_back(tid, thread_time.value());
        }
      });

  return !cpu_per_thread.empty();
}

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
base::expected<ProcessMemoryInfo, ProcessUsageError>
ProcessMetrics::GetMemoryInfo() const {
  std::string buffer;
  std::optional<StringViewPairs> pairs =
      internal::ReadProcFileToTrimmedStringPairs(process_, "status", &buffer);
  if (!pairs) {
    return base::unexpected(ProcessUsageError::kSystemError);
  }
  ProcessMemoryInfo dump;
  for (const auto& [key, value_str] : *pairs) {
    if (key == "VmSwap") {
      dump.vm_swap_bytes =
          static_cast<uint64_t>(GetKbFieldAsSizeT(value_str)) * 1024;
    } else if (key == "VmRSS") {
      dump.resident_set_bytes =
          static_cast<uint64_t>(GetKbFieldAsSizeT(value_str)) * 1024;
    } else if (key == "RssAnon") {
      dump.rss_anon_bytes =
          static_cast<uint64_t>(GetKbFieldAsSizeT(value_str)) * 1024;
    }
  }
  if (dump.rss_anon_bytes != 0) {
    return dump;
  }
  // RssAnon was introduced in Linux 4.5, use /proc/pid/statm as fallback.
  std::string statm_data;
  FilePath statm_file = internal::GetProcPidDir(process_).Append("statm");
  if (!internal::ReadProcFile(statm_file, &statm_data)) {
    return base::unexpected(ProcessUsageError::kSystemError);
  }
  std::vector<std::string_view> values = SplitStringPieceUsingSubstr(
      statm_data, " ", TRIM_WHITESPACE, SPLIT_WANT_NONEMPTY);
  CHECK_GE(values.size(), 3U);
  uint64_t resident_pages = 0;
  uint64_t shared_pages = 0;
  CHECK(StringToUint64(values[1], &resident_pages));
  CHECK(StringToUint64(values[2], &shared_pages));
  static const size_t page_size = GetPageSize();
  dump.rss_anon_bytes = (resident_pages - shared_pages) * page_size;
  return dump;
}

bool ProcessMetrics::GetPageFaultCounts(PageFaultCounts* counts) const {
  // We are not using internal::ReadStatsFileAndGetFieldAsInt64(), since it
  // would read the file twice, and return inconsistent numbers.
  std::string stats_data;
  if (!internal::ReadProcStats(process_, &stats_data)) {
    return false;
  }
  std::vector<std::string_view> proc_stats;
  if (!internal::ParseProcStats(stats_data, &proc_stats)) {
    return false;
  }

  counts->minor =
      internal::GetProcStatsFieldAsInt64(proc_stats, internal::VM_MINFLT);
  counts->major =
      internal::GetProcStatsFieldAsInt64(proc_stats, internal::VM_MAJFLT);
  return true;
}
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) ||
        // BUILDFLAG(IS_ANDROID)

int ProcessMetrics::GetOpenFdCount() const {
  // Use /proc/<pid>/fd to count the number of entries there.
  FilePath fd_path = internal::GetProcPidDir(process_).Append("fd");

  DirReaderPosix dir_reader(fd_path.value().c_str());
  if (!dir_reader.IsValid()) {
    return -1;
  }

  int total_count = 0;
  for (; dir_reader.Next();) {
    const char* name = dir_reader.name();
    if (UNSAFE_TODO(strcmp(name, ".")) != 0 &&
        UNSAFE_TODO(strcmp(name, "..")) != 0) {
      ++total_count;
    }
  }

  return total_count;
}

int ProcessMetrics::GetOpenFdSoftLimit() const {
  // Use /proc/<pid>/limits to read the open fd limit.
  FilePath fd_path = internal::GetProcPidDir(process_).Append("limits");

  std::string limits_contents;
  if (!ReadFileToStringNonBlocking(fd_path, &limits_contents)) {
    return -1;
  }

  for (const auto& line : SplitStringPiece(
           limits_contents, "\n", KEEP_WHITESPACE, SPLIT_WANT_NONEMPTY)) {
    if (!StartsWith(line, "Max open files")) {
      continue;
    }

    auto tokens =
        SplitStringPiece(line, " ", TRIM_WHITESPACE, SPLIT_WANT_NONEMPTY);
    if (tokens.size() > 3) {
      int limit = -1;
      if (!StringToInt(tokens[3], &limit)) {
        return -1;
      }
      return limit;
    }
  }
  return -1;
}

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_AIX)
ProcessMetrics::ProcessMetrics(ProcessHandle process)
    : process_(process), last_absolute_idle_wakeups_(0) {}
#else
ProcessMetrics::ProcessMetrics(ProcessHandle process) : process_(process) {}
#endif

size_t GetSystemCommitCharge() {
  SystemMemoryInfo meminfo;
  if (!GetSystemMemoryInfo(&meminfo)) {
    return 0;
  }
  return GetSystemCommitChargeFromMeminfo(meminfo);
}

size_t GetSystemCommitChargeFromMeminfo(const SystemMemoryInfo& meminfo) {
  // TODO(http://b/315988925): This math is incorrect: `cached` can be very
  // large so that `free` + `buffers` + `cached` > `total`. Replace this with a
  // more meaningful metric or remove it. In the meantime, convert underflows to
  // 0 instead of crashing.
  return ClampedNumeric<size_t>(meminfo.total.InKiB()) - meminfo.free.InKiB() -
         meminfo.buffers.InKiB() - meminfo.cached.InKiB();
}

int ParseProcStatCPU(std::string_view input) {
  // |input| may be empty if the process disappeared somehow.
  // e.g. http://crbug.com/145811.
  if (input.empty()) {
    return -1;
  }

  size_t start = input.find_last_of(')');
  if (start == input.npos) {
    return -1;
  }

  // Number of spaces remaining until reaching utime's index starting after the
  // last ')'.
  int num_spaces_remaining = internal::VM_UTIME - 1;

  size_t i = start;
  while ((i = input.find(' ', i + 1)) != input.npos) {
    // Validate the assumption that there aren't any contiguous spaces
    // in |input| before utime.
    DCHECK_NE(input[i - 1], ' ');
    if (--num_spaces_remaining == 0) {
      int utime = 0;
      int stime = 0;
      if (UNSAFE_TODO(sscanf(&input.data()[i], "%d %d", &utime, &stime)) != 2) {
        return -1;
      }

      return utime + stime;
    }
  }

  return -1;
}

int64_t GetNumberOfThreads(ProcessHandle process) {
  return internal::ReadProcStatsAndGetFieldAsInt64(process,
                                                   internal::VM_NUMTHREADS);
}

const char kProcSelfExe[] = "/proc/self/exe";

namespace {

// The format of /proc/diskstats is:
//  Device major number
//  Device minor number
//  Device name
//  Field  1 -- # of reads completed
//      This is the total number of reads completed successfully.
//  Field  2 -- # of reads merged, field 6 -- # of writes merged
//      Reads and writes which are adjacent to each other may be merged for
//      efficiency.  Thus two 4K reads may become one 8K read before it is
//      ultimately handed to the disk, and so it will be counted (and queued)
//      as only one I/O.  This field lets you know how often this was done.
//  Field  3 -- # of sectors read
//      This is the total number of sectors read successfully.
//  Field  4 -- # of milliseconds spent reading
//      This is the total number of milliseconds spent by all reads (as
//      measured from __make_request() to end_that_request_last()).
//  Field  5 -- # of writes completed
//      This is the total number of writes completed successfully.
//  Field  6 -- # of writes merged
//      See the description of field 2.
//  Field  7 -- # of sectors written
//      This is the total number of sectors written successfully.
//  Field  8 -- # of milliseconds spent writing
//      This is the total number of milliseconds spent by all writes (as
//      measured from __make_request() to end_that_request_last()).
//  Field  9 -- # of I/Os currently in progress
//      The only field that should go to zero. Incremented as requests are
//      given to appropriate struct request_queue and decremented as they
//      finish.
//  Field 10 -- # of milliseconds spent doing I/Os
//      This field increases so long as field 9 is nonzero.
//  Field 11 -- weighted # of milliseconds spent doing I/Os
//      This field is incremented at each I/O start, I/O completion, I/O
//      merge, or read of these stats by the number of I/Os in progress
//      (field 9) times the number of milliseconds spent doing I/O since the
//      last update of this field.  This can provide an easy measure of both
//      I/O completion time and the backlog that may be accumulating.

const size_t kDiskDriveName = 2;
const size_t kDiskReads = 3;
const size_t kDiskReadsMerged = 4;
const size_t kDiskSectorsRead = 5;
const size_t kDiskReadTime = 6;
const size_t kDiskWrites = 7;
const size_t kDiskWritesMerged = 8;
const size_t kDiskSectorsWritten = 9;
const size_t kDiskWriteTime = 10;
const size_t kDiskIO = 11;
const size_t kDiskIOTime = 12;
const size_t kDiskWeightedIOTime = 13;

}  // namespace

bool ParseProcMeminfo(std::string_view meminfo_data,
                      SystemMemoryInfo* meminfo) {
  // The format of /proc/meminfo is:
  //
  // MemTotal:      8235324 kB
  // MemFree:       1628304 kB
  // Buffers:        429596 kB
  // Cached:        4728232 kB
  // ...
  // There is no guarantee on the ordering or position
  // though it doesn't appear to change very often

  // As a basic sanity check at the end, make sure the MemTotal value will be at
  // least non-zero. So start off with a zero total.
  meminfo->total = ByteSize(0);

  for (std::string_view line : SplitStringPiece(
           meminfo_data, "\n", KEEP_WHITESPACE, SPLIT_WANT_NONEMPTY)) {
    std::vector<std::string_view> tokens = SplitStringPiece(
        line, kWhitespaceASCII, TRIM_WHITESPACE, SPLIT_WANT_NONEMPTY);
    // HugePages_* only has a number and no suffix so there may not be exactly 3
    // tokens.
    if (tokens.size() <= 1) {
      DLOG(WARNING) << "meminfo: tokens: " << tokens.size()
                    << " malformed line: " << line;
      continue;
    }

    ByteSize* target = nullptr;
    if (tokens[0] == "MemTotal:") {
      target = &meminfo->total;
    } else if (tokens[0] == "MemFree:") {
      target = &meminfo->free;
    } else if (tokens[0] == "MemAvailable:") {
      target = &meminfo->available;
    } else if (tokens[0] == "Buffers:") {
      target = &meminfo->buffers;
    } else if (tokens[0] == "Cached:") {
      target = &meminfo->cached;
    } else if (tokens[0] == "Active(anon):") {
      target = &meminfo->active_anon;
    } else if (tokens[0] == "Inactive(anon):") {
      target = &meminfo->inactive_anon;
    } else if (tokens[0] == "Active(file):") {
      target = &meminfo->active_file;
    } else if (tokens[0] == "Inactive(file):") {
      target = &meminfo->inactive_file;
    } else if (tokens[0] == "SwapTotal:") {
      target = &meminfo->swap_total;
    } else if (tokens[0] == "SwapFree:") {
      target = &meminfo->swap_free;
    } else if (tokens[0] == "Dirty:") {
      target = &meminfo->dirty;
    } else if (tokens[0] == "SReclaimable:") {
      target = &meminfo->reclaimable;
    }
    if (target) {
      uint64_t value;
      if (StringToUint64(tokens[1], &value) &&
          value <= ByteSize::Max().InKiB()) {
        *target = KiBU(value);
      }
    }
  }

  // Make sure the MemTotal is valid.
  return meminfo->total > ByteSize(0);
}

bool ParseProcVmstat(std::string_view vmstat_data, VmStatInfo* vmstat) {
  // The format of /proc/vmstat is:
  //
  // nr_free_pages 299878
  // nr_inactive_anon 239863
  // nr_active_anon 1318966
  // nr_inactive_file 2015629
  // ...
  //
  // Iterate through the whole file because the position of the
  // fields are dependent on the kernel version and configuration.

  // Returns true if all of these 3 fields are present.
  bool has_pswpin = false;
  bool has_pswpout = false;
  bool has_pgmajfault = false;

  // The oom_kill field is optional. The vmstat oom_kill field is available on
  // upstream kernel 4.13. It's backported to Chrome OS kernel 3.10.
  bool has_oom_kill = false;
  vmstat->oom_kill = 0;

  for (std::string_view line : SplitStringPiece(
           vmstat_data, "\n", KEEP_WHITESPACE, SPLIT_WANT_NONEMPTY)) {
    std::vector<std::string_view> tokens =
        SplitStringPiece(line, " ", KEEP_WHITESPACE, SPLIT_WANT_NONEMPTY);
    if (tokens.size() != 2) {
      continue;
    }

    uint64_t val;
    if (!StringToUint64(tokens[1], &val)) {
      continue;
    }

    if (tokens[0] == "pswpin") {
      vmstat->pswpin = val;
      DCHECK(!has_pswpin);
      has_pswpin = true;
    } else if (tokens[0] == "pswpout") {
      vmstat->pswpout = val;
      DCHECK(!has_pswpout);
      has_pswpout = true;
    } else if (tokens[0] == "pgmajfault") {
      vmstat->pgmajfault = val;
      DCHECK(!has_pgmajfault);
      has_pgmajfault = true;
    } else if (tokens[0] == "oom_kill") {
      vmstat->oom_kill = val;
      DCHECK(!has_oom_kill);
      has_oom_kill = true;
    }
  }

  return has_pswpin && has_pswpout && has_pgmajfault;
}

bool GetSystemMemoryInfo(SystemMemoryInfo* meminfo) {
  // Used memory is: total - free - buffers - caches
  // ReadFileToStringNonBlocking doesn't require ScopedAllowIO, and reading
  // /proc/meminfo is fast. See crbug.com/1160988 for details.
  FilePath meminfo_file("/proc/meminfo");
  std::string meminfo_data;
  if (!ReadFileToStringNonBlocking(meminfo_file, &meminfo_data)) {
    DLOG(WARNING) << "Failed to open " << meminfo_file.value();
    return false;
  }

  if (!ParseProcMeminfo(meminfo_data, meminfo)) {
    DLOG(WARNING) << "Failed to parse " << meminfo_file.value();
    return false;
  }

  return true;
}

bool GetVmStatInfo(VmStatInfo* vmstat) {
  // Synchronously reading files in /proc is safe.
  ScopedAllowBlockingForProcessMetrics allow_blocking;

  FilePath vmstat_file("/proc/vmstat");
  std::string vmstat_data;
  if (!ReadFileToStringNonBlocking(vmstat_file, &vmstat_data)) {
    DLOG(WARNING) << "Failed to open " << vmstat_file.value();
    return false;
  }
  if (!ParseProcVmstat(vmstat_data, vmstat)) {
    DLOG(WARNING) << "Failed to parse " << vmstat_file.value();
    return false;
  }
  return true;
}

SystemDiskInfo::SystemDiskInfo() {
  reads = 0;
  reads_merged = 0;
  sectors_read = 0;
  read_time = 0;
  writes = 0;
  writes_merged = 0;
  sectors_written = 0;
  write_time = 0;
  io = 0;
  io_time = 0;
  weighted_io_time = 0;
}

SystemDiskInfo::SystemDiskInfo(const SystemDiskInfo&) = default;

SystemDiskInfo& SystemDiskInfo::operator=(const SystemDiskInfo&) = default;

bool IsValidDiskName(std::string_view candidate) {
  if (candidate.length() < 3) {
    return false;
  }

  if (candidate[1] == 'd' &&
      (candidate[0] == 'h' || candidate[0] == 's' || candidate[0] == 'v')) {
    // [hsv]d[a-z]+ case
    for (size_t i = 2; i < candidate.length(); ++i) {
      if (!absl::ascii_islower(static_cast<unsigned char>(candidate[i]))) {
        return false;
      }
    }
    return true;
  }

  const char kMMCName[] = "mmcblk";
  if (!StartsWith(candidate, kMMCName)) {
    return false;
  }

  // mmcblk[0-9]+ case
  for (size_t i = strlen(kMMCName); i < candidate.length(); ++i) {
    if (!absl::ascii_isdigit(static_cast<unsigned char>(candidate[i]))) {
      return false;
    }
  }
  return true;
}

bool GetSystemDiskInfo(SystemDiskInfo* diskinfo) {
  // Synchronously reading files in /proc does not hit the disk.
  ScopedAllowBlockingForProcessMetrics allow_blocking;

  FilePath diskinfo_file("/proc/diskstats");
  std::string diskinfo_data;
  if (!ReadFileToStringNonBlocking(diskinfo_file, &diskinfo_data)) {
    DLOG(WARNING) << "Failed to open " << diskinfo_file.value();
    return false;
  }

  std::vector<std::string_view> diskinfo_lines = SplitStringPiece(
      diskinfo_data, "\n", KEEP_WHITESPACE, SPLIT_WANT_NONEMPTY);
  if (diskinfo_lines.empty()) {
    DLOG(WARNING) << "No lines found";
    return false;
  }

  diskinfo->reads = 0;
  diskinfo->reads_merged = 0;
  diskinfo->sectors_read = 0;
  diskinfo->read_time = 0;
  diskinfo->writes = 0;
  diskinfo->writes_merged = 0;
  diskinfo->sectors_written = 0;
  diskinfo->write_time = 0;
  diskinfo->io = 0;
  diskinfo->io_time = 0;
  diskinfo->weighted_io_time = 0;

  uint64_t reads = 0;
  uint64_t reads_merged = 0;
  uint64_t sectors_read = 0;
  uint64_t read_time = 0;
  uint64_t writes = 0;
  uint64_t writes_merged = 0;
  uint64_t sectors_written = 0;
  uint64_t write_time = 0;
  uint64_t io = 0;
  uint64_t io_time = 0;
  uint64_t weighted_io_time = 0;

  for (std::string_view line : diskinfo_lines) {
    std::vector<std::string_view> disk_fields = SplitStringPiece(
        line, kWhitespaceASCII, TRIM_WHITESPACE, SPLIT_WANT_NONEMPTY);

    // Fields may have overflowed and reset to zero.
    if (!IsValidDiskName(disk_fields[kDiskDriveName])) {
      continue;
    }

    StringToUint64(disk_fields[kDiskReads], &reads);
    StringToUint64(disk_fields[kDiskReadsMerged], &reads_merged);
    StringToUint64(disk_fields[kDiskSectorsRead], &sectors_read);
    StringToUint64(disk_fields[kDiskReadTime], &read_time);
    StringToUint64(disk_fields[kDiskWrites], &writes);
    StringToUint64(disk_fields[kDiskWritesMerged], &writes_merged);
    StringToUint64(disk_fields[kDiskSectorsWritten], &sectors_written);
    StringToUint64(disk_fields[kDiskWriteTime], &write_time);
    StringToUint64(disk_fields[kDiskIO], &io);
    StringToUint64(disk_fields[kDiskIOTime], &io_time);
    StringToUint64(disk_fields[kDiskWeightedIOTime], &weighted_io_time);

    diskinfo->reads += reads;
    diskinfo->reads_merged += reads_merged;
    diskinfo->sectors_read += sectors_read;
    diskinfo->read_time += read_time;
    diskinfo->writes += writes;
    diskinfo->writes_merged += writes_merged;
    diskinfo->sectors_written += sectors_written;
    diskinfo->write_time += write_time;
    diskinfo->io += io;
    diskinfo->io_time += io_time;
    diskinfo->weighted_io_time += weighted_io_time;
  }

  return true;
}

TimeDelta GetUserCpuTimeSinceBoot() {
  return internal::GetUserCpuTimeSinceBoot();
}


#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_AIX)
int ProcessMetrics::GetIdleWakeupsPerSecond() {
  uint64_t num_switches;
  static const char kSwitchStat[] = "voluntary_ctxt_switches";
  return internal::ReadProcStatusAndGetFieldAsUint64(process_, kSwitchStat,
                                                     &num_switches)
             ? CalculateIdleWakeupsPerSecond(num_switches)
             : 0;
}
#endif  // BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_AIX)

ByteSize SystemMemoryInfo::GetAvailablePhysicalMemory() const {
  // Use MemAvailable from /proc/meminfo if available (Linux 3.14+), otherwise
  // fall back to MemFree.
  return available.is_positive() ? available : free;
}

}  // namespace base
