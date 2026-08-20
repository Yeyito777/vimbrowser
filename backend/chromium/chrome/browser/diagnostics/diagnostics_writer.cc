// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/diagnostics/diagnostics_writer.h"

#include <stdint.h>

#include <string>
#include <string_view>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/common/chrome_switches.h"
#include "ui/base/ui_base_paths.h"

#include <stdio.h>
#include <unistd.h>

namespace diagnostics {

// This is a minimalistic interface to wrap the platform console.
class SimpleConsole {
 public:
  enum Color {
    DEFAULT,
    RED,
    GREEN,
  };

  virtual ~SimpleConsole() = default;

  // Init must be called before using any other method. If it returns
  // false there will be no console output.
  virtual bool Init() = 0;

  // Writes a string to the console with the current color.
  virtual bool Write(const std::u16string& text) = 0;

  // Called when the program is about to exit.
  virtual void OnQuit() = 0;

  // Sets the foreground text color.
  virtual bool SetColor(Color color) = 0;

  // Create an appropriate SimpleConsole instance.  May return NULL if there is
  // no implementation for the current platform.
  static SimpleConsole* Create();
};

namespace {

class PosixConsole : public SimpleConsole {
 public:
  PosixConsole() : use_color_(false) {}

  PosixConsole(const PosixConsole&) = delete;
  PosixConsole& operator=(const PosixConsole&) = delete;

  bool Init() override {
    // Technically, we should also check the terminal capabilities before using
    // color, but in practice this is unlikely to be an issue.
    use_color_ = isatty(STDOUT_FILENO);
    return true;
  }

  bool Write(const std::u16string& text) override {
    // We're assuming that the terminal is using UTF-8 encoding.
    printf("%s", base::UTF16ToUTF8(text).c_str());
    return true;
  }

  void OnQuit() override {
    // The "press enter to continue" prompt isn't very unixy, so only do that on
    // Windows.
  }

  bool SetColor(Color color) override {
    if (!use_color_)
      return false;

    const char* code = "\033[m";
    switch (color) {
      case RED:
        code = "\033[1;31m";
        break;
      case GREEN:
        code = "\033[1;32m";
        break;
      case DEFAULT:
        break;
      default:
        NOTREACHED();
    }
    UNSAFE_TODO(printf("%s", code));
    return true;
  }

 private:
  bool use_color_;
};

}  // namespace

SimpleConsole* SimpleConsole::Create() { return new PosixConsole(); }


///////////////////////////////////////////////////////////
//  DiagnosticsWriter

DiagnosticsWriter::DiagnosticsWriter(FormatType format)
    : failures_(0), format_(format) {
  // Only create consoles for non-log output.
  if (format_ != LOG) {
    console_.reset(SimpleConsole::Create());
    console_->Init();
  }
}

DiagnosticsWriter::~DiagnosticsWriter() {
  if (console_.get())
    console_->OnQuit();
}

bool DiagnosticsWriter::WriteInfoLine(const std::string& info_text) {
  if (format_ == LOG) {
    LOG(WARNING) << info_text;
    return true;
  } else {
    if (console_.get()) {
      console_->SetColor(SimpleConsole::DEFAULT);
      console_->Write(base::UTF8ToUTF16(info_text + "\n"));
    }
  }
  return true;
}

void DiagnosticsWriter::OnTestFinished(int index, DiagnosticsModel* model) {
  const DiagnosticsModel::TestInfo& test_info = model->GetTest(index);
  bool success = (DiagnosticsModel::TEST_OK == test_info.GetResult());
  WriteResult(success,
              test_info.GetName(),
              test_info.GetTitle(),
              test_info.GetOutcomeCode(),
              test_info.GetAdditionalInfo());
}

void DiagnosticsWriter::OnAllTestsDone(DiagnosticsModel* model) {
  WriteInfoLine(
      base::StringPrintf("Finished %d tests.", model->GetTestRunCount()));
}

void DiagnosticsWriter::OnRecoveryFinished(int index, DiagnosticsModel* model) {
  const DiagnosticsModel::TestInfo& test_info = model->GetTest(index);
  WriteInfoLine(
      base::StringPrintf("Finished Recovery for: %s", test_info.GetTitle()));
}

void DiagnosticsWriter::OnAllRecoveryDone(DiagnosticsModel* model) {
  WriteInfoLine("Finished All Recovery operations.");
}

bool DiagnosticsWriter::WriteResult(bool success,
                                    std::string_view id,
                                    std::string_view name,
                                    int outcome_code,
                                    const std::string& extra) {
  std::string result;
  SimpleConsole::Color color;

  if (success) {
    result = "[PASS] ";
    color = SimpleConsole::GREEN;
  } else {
    color = SimpleConsole::RED;
    result = "[FAIL] ";
    failures_++;
  }

  if (format_ != LOG) {
    if (console_.get()) {
      console_->SetColor(color);
      console_->Write(base::ASCIIToUTF16(result));
    }
    if (format_ == MACHINE) {
      return WriteInfoLine(
          base::StringPrintf("%03d %s (%s)", outcome_code, id, extra.c_str()));
    } else {
      return WriteInfoLine(base::StringPrintf("%s\n       %s\n", name, extra));
    }
  } else {
    if (!success) {
      // For log output, we only care about the tests that failed.
      return WriteInfoLine(base::StringPrintf("%s%03d %s (%s)", result.c_str(),
                                              outcome_code, id, extra.c_str()));
    }
  }
  return true;
}

}  // namespace diagnostics
