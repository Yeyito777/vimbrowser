// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_APP_SHELL_CRASH_REPORTER_CLIENT_H_
#define CONTENT_SHELL_APP_SHELL_CRASH_REPORTER_CLIENT_H_

#include "build/build_config.h"
#include "components/crash/core/app/crash_reporter_client.h"

namespace content {

class ShellCrashReporterClient : public crash_reporter::CrashReporterClient {
 public:
  ShellCrashReporterClient();

  ShellCrashReporterClient(const ShellCrashReporterClient&) = delete;
  ShellCrashReporterClient& operator=(const ShellCrashReporterClient&) = delete;

  ~ShellCrashReporterClient() override;


#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_MAC)
  base::FilePath GetReporterLogFilename() override;
#endif

  // The location where minidump files should be written. Returns true if
  // |crash_dir| was set.
  bool GetCrashDumpLocation(base::FilePath* crash_dir) override;

  void GetProductInfo(ProductInfo* product_info) override;
  bool EnableBreakpadForProcess(const std::string& process_type) override;
};

}  // namespace content

#endif  // CONTENT_SHELL_APP_SHELL_CRASH_REPORTER_CLIENT_H_
