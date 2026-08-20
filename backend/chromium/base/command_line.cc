// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"

#include <algorithm>
#include <array>
#include <ostream>
#include <string_view>

#include "base/check_op.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/debug/debugging_buildflags.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/notreached.h"
#include "base/numerics/checked_math.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"


namespace base {

CommandLine* CommandLine::current_process_commandline_ = nullptr;

namespace {

DuplicateSwitchHandler* g_duplicate_switch_handler = nullptr;

constexpr CommandLine::CharType kSwitchTerminator[] = FILE_PATH_LITERAL("--");
constexpr CommandLine::CharType kSwitchValueSeparator[] =
    FILE_PATH_LITERAL("=");

// Since we use a lazy match, make sure that longer versions (like "--") are
// listed before shorter versions (like "-") of similar prefixes.
#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
// Unixes don't use slash as a switch.
constexpr auto kSwitchPrefixes = std::to_array<CommandLine::StringViewType>({
    "--",
    "-",
});
#endif
size_t switch_prefix_count = std::size(kSwitchPrefixes);

bool IsSwitchNameValid(std::string_view switch_name) {
  return ToLowerASCII(switch_name) == switch_name;
}


size_t GetSwitchPrefixLength(CommandLine::StringViewType string) {
  for (size_t i = 0; i < switch_prefix_count; ++i) {
    CommandLine::StringType prefix(kSwitchPrefixes[i]);
    if (string.substr(0, prefix.length()) == prefix) {
      return prefix.length();
    }
  }
  return 0;
}

// Fills in |switch_string| and |switch_value| if |string| is a switch.
// This will preserve the input switch prefix in the output |switch_string|.
bool IsSwitch(const CommandLine::StringType& string,
              CommandLine::StringType* switch_string,
              CommandLine::StringType* switch_value) {
  switch_string->clear();
  switch_value->clear();
  size_t prefix_length = GetSwitchPrefixLength(string);
  if (prefix_length == 0 || prefix_length == string.length()) {
    return false;
  }

  const size_t equals_position = string.find(kSwitchValueSeparator);
  *switch_string = string.substr(0, equals_position);
  if (equals_position != CommandLine::StringType::npos) {
    *switch_value = string.substr(equals_position + 1);
  }
  return true;
}

// Returns true iff |string| represents a switch with key
// |switch_key_without_prefix|, regardless of value.
bool IsSwitchWithKey(CommandLine::StringViewType string,
                     CommandLine::StringViewType switch_key_without_prefix) {
  size_t prefix_length = GetSwitchPrefixLength(string);
  if (prefix_length == 0 || prefix_length == string.length()) {
    return false;
  }

  const size_t equals_position = string.find(kSwitchValueSeparator);
  return string.substr(prefix_length, equals_position - prefix_length) ==
         switch_key_without_prefix;
}


}  // namespace

// static
void CommandLine::SetDuplicateSwitchHandler(
    std::unique_ptr<DuplicateSwitchHandler> new_duplicate_switch_handler) {
  delete g_duplicate_switch_handler;
  g_duplicate_switch_handler = new_duplicate_switch_handler.release();
}

CommandLine::CommandLine(NoProgram no_program) : argv_(1), begin_args_(1) {}

CommandLine::CommandLine(const FilePath& program) : argv_(1), begin_args_(1) {
  SetProgram(program);
}

CommandLine::CommandLine(int argc, const CommandLine::CharType* const* argv)
    : argv_(1), begin_args_(1) {
  // SAFETY: required from caller.
  UNSAFE_BUFFERS(InitFromArgv(argc, argv));
}

CommandLine::CommandLine(const StringVector& argv) : argv_(1), begin_args_(1) {
  InitFromArgv(argv);
}

CommandLine::CommandLine(const CommandLine& other) = default;
CommandLine::CommandLine(CommandLine&& other) noexcept
    :
      argv_(std::exchange(other.argv_, StringVector(1))),
      switches_(std::move(other.switches_)),
      begin_args_(std::exchange(other.begin_args_, 1)) {
#if BUILDFLAG(ENABLE_COMMANDLINE_SEQUENCE_CHECKS)
  other.sequence_checker_.Detach();
#endif
}
CommandLine& CommandLine::operator=(const CommandLine& other) = default;
CommandLine& CommandLine::operator=(CommandLine&& other) noexcept {
  argv_ = std::exchange(other.argv_, StringVector(1));
  switches_ = std::move(other.switches_);
  begin_args_ = std::exchange(other.begin_args_, 1);
#if BUILDFLAG(ENABLE_COMMANDLINE_SEQUENCE_CHECKS)
  other.sequence_checker_.Detach();
#endif
  return *this;
}
CommandLine::~CommandLine() = default;


// static
bool CommandLine::Init(int argc, const char* const* argv) {
  if (current_process_commandline_) {
    // If this is intentional, Reset() must be called first. If we are using
    // the shared build mode, we have to share a single object across multiple
    // shared libraries.
    return false;
  }

  current_process_commandline_ = new CommandLine(NO_PROGRAM);
#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  // SAFETY: required from caller.
  UNSAFE_BUFFERS(current_process_commandline_->InitFromArgv(argc, argv));
#else
#error Unsupported platform
#endif

  return true;
}

// static
bool CommandLine::Init(const StringVector& argv) {
  if (current_process_commandline_) {
    // If this is intentional, Reset() must be called first. If we are using
    // the shared build mode, we have to share a single object across multiple
    // shared libraries.
    return false;
  }

  current_process_commandline_ = new CommandLine(argv);
  return true;
}

// static
void CommandLine::Reset() {
  DCHECK(current_process_commandline_);
  delete current_process_commandline_;
  current_process_commandline_ = nullptr;
}

// static
CommandLine* CommandLine::ForCurrentProcess() {
  DCHECK(current_process_commandline_);
  return current_process_commandline_;
}

// static
bool CommandLine::InitializedForCurrentProcess() {
  return !!current_process_commandline_;
}

// static
CommandLine CommandLine::FromArgvWithoutProgram(const StringVector& argv) {
  CommandLine cmd(NO_PROGRAM);
  cmd.AppendSwitchesAndArguments(argv);
  return cmd;
}


void CommandLine::InitFromArgv(int argc,
                               const CommandLine::CharType* const* argv) {
  StringVector new_argv;
  for (int i = 0; i < argc; ++i) {
    // SAFETY: required from caller.
    new_argv.push_back(UNSAFE_BUFFERS(argv[i]));
  }
  InitFromArgv(new_argv);
}

void CommandLine::InitFromArgv(const StringVector& argv) {
  argv_ = StringVector(1);
  switches_.clear();
  begin_args_ = 1;
  SetProgram(argv.empty() ? FilePath() : FilePath(argv[0]));
  if (!argv.empty()) {
    AppendSwitchesAndArguments(span(argv).subspan<1>());
  }
}

FilePath CommandLine::GetProgram() const {
  return FilePath(argv_[0]);
}

void CommandLine::SetProgram(const FilePath& program) {
#if BUILDFLAG(ENABLE_COMMANDLINE_SEQUENCE_CHECKS)
  sequence_checker_.Check();
#endif
#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  TrimWhitespaceASCII(program.value(), TRIM_ALL, &argv_[0]);
#else
#error Unsupported platform
#endif
}

bool CommandLine::HasSwitch(std::string_view switch_string) const {
  CHECK(IsSwitchNameValid(switch_string));
  return switches_.contains(switch_string);
}

bool CommandLine::HasSwitch(const char switch_constant[]) const {
  return HasSwitch(std::string_view(switch_constant));
}

std::string CommandLine::GetSwitchValueASCII(
    std::string_view switch_string) const {
  StringType value = GetSwitchValueNative(switch_string);
#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  if (!IsStringASCII(value)) {
#endif
    DLOG(WARNING) << "Value of switch (" << switch_string << ") must be ASCII.";
    return std::string();
  }
#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  return value;
#endif
}

std::string CommandLine::GetSwitchValueUTF8(
    std::string_view switch_string) const {
  StringType value = GetSwitchValueNative(switch_string);

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  const std::string maybe_utf8_value = value;
#endif

  if (!IsStringUTF8(maybe_utf8_value)) {
    DLOG(WARNING) << "Value of switch (" << switch_string << ") is not UTF8.";
    return {};
  }
  return maybe_utf8_value;
}

FilePath CommandLine::GetSwitchValuePath(std::string_view switch_string) const {
  return FilePath(GetSwitchValueNative(switch_string));
}

CommandLine::StringType CommandLine::GetSwitchValueNative(
    std::string_view switch_string) const {
  CHECK(IsSwitchNameValid(switch_string));

  auto result = switches_.find(switch_string);
  return result == switches_.end() ? StringType() : result->second;
}

void CommandLine::AppendSwitch(std::string_view switch_string) {
  AppendSwitchNative(switch_string, StringType());
}

void CommandLine::AppendSwitchPath(std::string_view switch_string,
                                   const FilePath& path) {
  AppendSwitchNative(switch_string, path.value());
}

void CommandLine::AppendSwitchNative(std::string_view switch_string,
                                     CommandLine::StringViewType value) {
#if BUILDFLAG(ENABLE_COMMANDLINE_SEQUENCE_CHECKS)
  sequence_checker_.Check();
#endif
  const std::string switch_key = ToLowerASCII(switch_string);
#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  StringType combined_switch_string(switch_key);
#endif
  size_t prefix_length = GetSwitchPrefixLength(combined_switch_string);
  auto key = switch_key.substr(prefix_length);
  if (g_duplicate_switch_handler) {
    g_duplicate_switch_handler->ResolveDuplicate(key, value,
                                                 switches_[std::string(key)]);
  } else {
    switches_[std::string(key)] = StringType(value);
  }

  // Preserve existing switch prefixes in |argv_|; only append one if necessary.
  if (prefix_length == 0) {
    combined_switch_string.insert(0, kSwitchPrefixes[0].data(),
                                  kSwitchPrefixes[0].size());
  }
  if (!value.empty()) {
    base::StrAppend(&combined_switch_string, {kSwitchValueSeparator, value});
  }
  // Append the switch and update the switches/arguments divider |begin_args_|.
  argv_.insert(argv_.begin() + begin_args_, combined_switch_string);
  begin_args_ = (CheckedNumeric(begin_args_) + 1).ValueOrDie();
}

void CommandLine::AppendSwitchASCII(std::string_view switch_string,
                                    std::string_view value_string) {
  AppendSwitchUTF8(switch_string, value_string);
}

void CommandLine::AppendSwitchUTF8(std::string_view switch_string,
                                   std::string_view value_string) {
  DCHECK(IsStringUTF8(value_string))
      << "Switch (" << switch_string << ") value (" << value_string
      << ") is not UTF8.";
#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  AppendSwitchNative(switch_string, value_string);
#else
#error Unsupported platform
#endif
}

void CommandLine::RemoveSwitch(std::string_view switch_key_without_prefix) {
#if BUILDFLAG(ENABLE_COMMANDLINE_SEQUENCE_CHECKS)
  sequence_checker_.Check();
#endif
  CHECK(IsSwitchNameValid(switch_key_without_prefix));

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  StringType switch_key_native(switch_key_without_prefix);
#endif

  DCHECK_EQ(0u, GetSwitchPrefixLength(switch_key_native));
  auto it = switches_.find(switch_key_without_prefix);
  if (it == switches_.end()) {
    return;
  }
  switches_.erase(it);
  // Also erase from the switches section of |argv_| and update |begin_args_|
  // accordingly.
  // Switches in |argv_| have indices [1, begin_args_).
  auto argv_switches_begin = argv_.begin() + 1;
  auto argv_switches_end = argv_.begin() + begin_args_;
  DCHECK(argv_switches_begin <= argv_switches_end);
  DCHECK(argv_switches_end <= argv_.end());
  auto expell = std::remove_if(argv_switches_begin, argv_switches_end,
                               [&switch_key_native](const StringType& arg) {
                                 return IsSwitchWithKey(arg, switch_key_native);
                               });
  if (expell == argv_switches_end) {
    NOTREACHED();
  }
  begin_args_ -= argv_switches_end - expell;
  argv_.erase(expell, argv_switches_end);
}

void CommandLine::CopySwitchesFrom(const CommandLine& source,
                                   span<const char* const> switches) {
  for (const char* entry : switches) {
    if (source.HasSwitch(entry)) {
      AppendSwitchNative(entry, source.GetSwitchValueNative(entry));
    }
  }
}

CommandLine::StringVector CommandLine::GetArgs() const {
  // Gather all arguments after the last switch (may include kSwitchTerminator).
  StringVector args(argv_.begin() + begin_args_, argv_.end());
  // Erase only the first kSwitchTerminator (maybe "--" is a legitimate page?)
  auto switch_terminator = std::ranges::find(args, kSwitchTerminator);
  if (switch_terminator != args.end()) {
    args.erase(switch_terminator);
  }
  return args;
}

void CommandLine::AppendArg(std::string_view value) {
#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
  AppendArgNative(value);
#else
#error Unsupported platform
#endif
}

void CommandLine::AppendArgPath(const FilePath& path) {
  AppendArgNative(path.value());
}

void CommandLine::AppendArgNative(StringViewType value) {
#if BUILDFLAG(ENABLE_COMMANDLINE_SEQUENCE_CHECKS)
  sequence_checker_.Check();
#endif
  argv_.emplace_back(value);
}

void CommandLine::AppendArguments(const CommandLine& other,
                                  bool include_program) {
  if (include_program) {
    SetProgram(other.GetProgram());
  }
  if (!other.argv().empty()) {
    AppendSwitchesAndArguments(span(other.argv()).subspan<1>());
  }
}

void CommandLine::PrependWrapper(StringViewType wrapper) {
#if BUILDFLAG(ENABLE_COMMANDLINE_SEQUENCE_CHECKS)
  sequence_checker_.Check();
#endif
  if (wrapper.empty()) {
    return;
  }
  // Split the wrapper command based on whitespace (with quoting).
  // StringViewType does not currently work directly with StringTokenizerT.
  using CommandLineTokenizer =
      StringTokenizerT<StringType, StringType::const_iterator>;
  StringType wrapper_string(wrapper);
  CommandLineTokenizer tokenizer(wrapper_string, FILE_PATH_LITERAL(" "));
  tokenizer.set_quote_chars(FILE_PATH_LITERAL("'\""));
  std::vector<StringType> wrapper_argv;
  while (std::optional<StringViewType> token = tokenizer.GetNextTokenView()) {
    wrapper_argv.emplace_back(token.value());
  }

  // Prepend the wrapper and update the switches/arguments |begin_args_|.
  argv_.insert(argv_.begin(), wrapper_argv.begin(), wrapper_argv.end());
  begin_args_ += wrapper_argv.size();
}


void CommandLine::AppendSwitchesAndArguments(span<const StringType> argv) {
  bool parse_switches = true;
  for (StringType arg : argv) {
#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
    TrimWhitespaceASCII(arg, TRIM_ALL, &arg);
#endif

    CommandLine::StringType switch_string;
    CommandLine::StringType switch_value;
    parse_switches &= (arg != kSwitchTerminator);
    if (parse_switches && IsSwitch(arg, &switch_string, &switch_value)) {
#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
      AppendSwitchNative(switch_string, switch_value);
#else
#error Unsupported platform
#endif
    } else {
      AppendArgNative(arg);
    }
  }
}

CommandLine::StringType CommandLine::GetArgumentsStringInternal(
    bool allow_unsafe_insert_sequences) const {
  StringType params;
  // Append switches and arguments.
  bool parse_switches = true;

  for (size_t i = 1; i < argv_.size(); ++i) {
    StringType arg = argv_[i];
    StringType switch_string;
    StringType switch_value;
    parse_switches &= arg != kSwitchTerminator;
    if (i > 1) {
      params.append(FILE_PATH_LITERAL(" "));
    }
    if (parse_switches && IsSwitch(arg, &switch_string, &switch_value)) {
      params.append(switch_string);
      if (!switch_value.empty()) {
        params.append(kSwitchValueSeparator + switch_value);
      }
    } else {
      params.append(arg);
    }
  }
  return params;
}

CommandLine::StringType CommandLine::GetCommandLineString() const {
  StringType string(argv_[0]);
  StringType params(GetArgumentsString());
  if (!params.empty()) {
    string.append(FILE_PATH_LITERAL(" "));
    string.append(params);
  }
  return string;
}


CommandLine::StringType CommandLine::GetArgumentsString() const {
  return GetArgumentsStringInternal(/*allow_unsafe_insert_sequences=*/false);
}


void CommandLine::DetachFromCurrentSequence() {
#if BUILDFLAG(ENABLE_COMMANDLINE_SEQUENCE_CHECKS)
  sequence_checker_.Detach();
#endif  // BUILDFLAG(ENABLE_COMMANDLINE_SEQUENCE_CHECKS)
}

}  // namespace base
