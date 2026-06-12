#include "config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>
#include <unistd.h>

namespace vimbrowser {
namespace {

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool ParseBoolSetting(std::string value, bool fallback) {
  value = ToLowerAscii(std::move(value));
  if (value == "1" || value == "true" || value == "on" || value == "yes") {
    return true;
  }
  if (value == "0" || value == "false" || value == "off" || value == "no") {
    return false;
  }
  return fallback;
}

bool ContainsAsciiWhitespace(std::string_view text) {
  return std::any_of(text.begin(), text.end(), [](unsigned char c) {
    return std::isspace(c);
  });
}

bool HasHandledUrlScheme(std::string_view text) {
  return StartsWith(text, "http://") || StartsWith(text, "https://") ||
         StartsWith(text, "file://") || StartsWith(text, "data:") ||
         StartsWith(text, "about:") || StartsWith(text, "chrome://");
}

bool LooksLikeUrl(std::string_view text) {
  if (HasHandledUrlScheme(text)) {
    return true;
  }

  // Bare host detection must be conservative.  Users often type searches with
  // dots in them ("gpt5.5 scores", "foo.bar error"), and treating the whole
  // string as a hostname makes Chromium percent-encode the spaces into the
  // host and navigate to e.g. https://gpt5.5%20scores/.  If a bare input has
  // whitespace, it is a search query; explicit schemes and local paths are
  // handled separately.
  if (ContainsAsciiWhitespace(text)) {
    return false;
  }

  return text.find('.') != std::string_view::npos || StartsWith(text, "localhost");
}

std::string PercentEncode(std::string_view text) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : text) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else if (c == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(kHex[(c >> 4) & 0xF]);
      out.push_back(kHex[c & 0xF]);
    }
  }
  return out;
}

std::string PercentEncodeFilePath(std::string_view text) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : text) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~' || c == '/') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(kHex[(c >> 4) & 0xF]);
      out.push_back(kHex[c & 0xF]);
    }
  }
  return out;
}

std::filesystem::path LaunchDirectory() {
  if (const char* launch_cwd = std::getenv("VIMBROWSER_LAUNCH_CWD");
      launch_cwd && *launch_cwd) {
    return std::filesystem::path(launch_cwd);
  }
  std::error_code ec;
  std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (!ec && !cwd.empty()) {
    return cwd;
  }
  return ".";
}

std::filesystem::path ExpandLocalPath(std::string_view input) {
  std::string value(input);
  if (value == "~" || StartsWith(value, "~/")) {
    if (const char* home = std::getenv("HOME"); home && *home) {
      value = std::string(home) + value.substr(1);
    }
  }

  std::filesystem::path path(value);
  if (path.is_absolute()) {
    return path;
  }
  return LaunchDirectory() / path;
}

bool LooksLikeLocalPath(std::string_view input) {
  if (input.empty() || HasHandledUrlScheme(input)) {
    return false;
  }
  if (input.front() == '/' || input == "~" || StartsWith(input, "~/") ||
      StartsWith(input, "./") || StartsWith(input, "../")) {
    return true;
  }

  std::error_code ec;
  return std::filesystem::exists(ExpandLocalPath(input), ec) && !ec;
}

std::string FileUrlForLocalPath(std::string_view input) {
  std::filesystem::path path = ExpandLocalPath(input);
  std::error_code ec;
  std::filesystem::path absolute = std::filesystem::weakly_canonical(path, ec);
  if (ec || absolute.empty()) {
    ec.clear();
    absolute = std::filesystem::absolute(path, ec);
  }
  if (ec || absolute.empty()) {
    absolute = path.lexically_normal();
  }
  return "file://" + PercentEncodeFilePath(absolute.lexically_normal().generic_string());
}

std::string RuntimeHome() {
  if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"); xdg && *xdg) {
    return std::string(xdg);
  }
  return "/tmp/vimbrowser-" + std::to_string(getuid());
}

std::string DefaultInstanceCachePath() {
  return RuntimeHome() + "/vimbrowser/cef/instances/" +
         std::to_string(getpid());
}

std::string DefaultInstanceStatePath() {
  return RuntimeHome() + "/vimbrowser/instances/" + std::to_string(getpid()) +
         "/state";
}

std::string EscapeStateValue(std::string_view value) {
  std::string out;
  for (char c : value) {
    if (c == '\\') {
      out += "\\\\";
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string UnescapeStateValue(std::string_view value) {
  std::string out;
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] != '\\' || i + 1 >= value.size()) {
      out.push_back(value[i]);
      continue;
    }
    const char escaped = value[++i];
    if (escaped == 'n') {
      out.push_back('\n');
    } else if (escaped == 'r') {
      out.push_back('\r');
    } else {
      out.push_back(escaped);
    }
  }
  return out;
}

void ReadPermissionDecision(std::string_view payload,
                            std::map<std::string, uint32_t>* decisions) {
  if (!decisions) {
    return;
  }
  const size_t tab = payload.find('\t');
  if (tab == std::string_view::npos) {
    return;
  }

  const std::string mask_text(payload.substr(0, tab));
  char* end = nullptr;
  const unsigned long mask = std::strtoul(mask_text.c_str(), &end, 10);
  if (end == mask_text.c_str() || (end && *end != '\0') ||
      mask > 0xffffffffUL) {
    return;
  }

  const std::string origin = UnescapeStateValue(payload.substr(tab + 1));
  if (origin.empty()) {
    return;
  }
  (*decisions)[origin] |= static_cast<uint32_t>(mask);
}

std::string ValueAfter(std::string_view arg, std::string_view prefix) {
  return std::string(arg.substr(prefix.size()));
}

std::string AbsolutePath(std::string path) {
  if (path.empty()) {
    return path;
  }
  return std::filesystem::absolute(std::filesystem::path(path))
      .lexically_normal()
      .string();
}

std::string ShellQuote(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('\'');
  for (char c : value) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out.push_back('\'');
  return out;
}

bool IsDisabledDwmSaveValue(std::string value) {
  value = ToLowerAscii(std::move(value));
  return value == "0" || value == "false" || value == "off" ||
         value == "no" || value == "none" || value == "disabled";
}

std::string BuildDefaultDwmSaveArgv(const Config& config) {
  // vimbrowser's normal installed entrypoint is the ~/.local/bin/vimbrowser
  // wrapper.  Save that stable command (rather than ./vimbrowser from the build
  // directory) and include the persistent profile so dwm restore opens the same
  // browser state qutebrowser restores via its --basedir registration.
  if (!config.explicit_profile_dir || config.profile_dir.empty()) {
    return {};
  }

  std::ostringstream command;
  command << "vimbrowser --profile-dir " << ShellQuote(config.profile_dir);
  return command.str();
}

void ApplyProfileDir(Config& config, std::string profile_dir) {
  config.profile_dir = AbsolutePath(std::move(profile_dir));
  config.cache_path = config.profile_dir + "/cef";
  config.state_path = config.profile_dir + "/state";
  config.explicit_profile_dir = true;
}

}  // namespace

std::string DefaultStatePath() {
  return DefaultInstanceStatePath();
}

const std::vector<SearchEngine>& SearchEngines() {
  static const std::vector<SearchEngine> engines = {
      {"yt", "https://www.youtube.com/results?search_query={}"},
      {"gh", "https://github.com/search?q={}"},
  };
  return engines;
}

const SearchEngine* FindSearchEngine(std::string_view name) {
  const std::string folded = ToLowerAscii(std::string(name));
  for (const SearchEngine& engine : SearchEngines()) {
    if (engine.name == folded) {
      return &engine;
    }
  }
  return nullptr;
}

std::string ResolveSearchEngineUrl(std::string_view name,
                                   std::string_view query) {
  const SearchEngine* engine = FindSearchEngine(name);
  if (!engine) {
    return ResolveUrlOrSearch(std::string(query));
  }

  std::string url = engine->url_template;
  const std::string encoded = PercentEncode(query);
  const size_t placeholder = url.find("{}");
  if (placeholder == std::string::npos) {
    return url + encoded;
  }
  url.replace(placeholder, 2, encoded);
  return url;
}

AppState ReadAppState(const std::string& state_path) {
  AppState state;
  std::ifstream file(state_path);
  if (!file) {
    return state;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (StartsWith(line, "tab=")) {
      const std::string tab = UnescapeStateValue(std::string_view(line).substr(4));
      if (!tab.empty()) {
        state.tabs.push_back(tab);
      }
    } else if (StartsWith(line, "open_history=")) {
      const std::string entry = UnescapeStateValue(std::string_view(line).substr(13));
      if (!entry.empty()) {
        state.open_history.push_back(entry);
      }
    } else if (StartsWith(line, "search_history_")) {
      const size_t equals = line.find('=');
      if (equals != std::string::npos && equals > 15) {
        const std::string engine = ToLowerAscii(line.substr(15, equals - 15));
        const std::string entry =
            UnescapeStateValue(std::string_view(line).substr(equals + 1));
        if (FindSearchEngine(engine) && !entry.empty()) {
          state.search_history[engine].push_back(entry);
        }
      }
    } else if (StartsWith(line, "media_permission_grant=")) {
      ReadPermissionDecision(std::string_view(line).substr(23),
                             &state.media_permission_grants);
    } else if (StartsWith(line, "media_permission_deny=")) {
      ReadPermissionDecision(std::string_view(line).substr(22),
                             &state.media_permission_denials);
    } else if (StartsWith(line, "active=")) {
      const std::string value = line.substr(7);
      char* end = nullptr;
      const unsigned long long active = std::strtoull(value.c_str(), &end, 10);
      if (end != value.c_str()) {
        state.active_index = static_cast<size_t>(active);
      }
    } else if (StartsWith(line, "showmode=")) {
      const std::string value = ToLowerAscii(std::string(line.substr(9)));
      state.show_mode_indicator = value == "1" || value == "true" ||
                                  value == "on" || value == "yes";
    } else if (StartsWith(line, "showfps=")) {
      const std::string value = ToLowerAscii(std::string(line.substr(8)));
      state.show_fps_indicator = value == "1" || value == "true" ||
                                 value == "on" || value == "yes";
    } else if (StartsWith(line, "showstatusline=")) {
      const std::string value = ToLowerAscii(std::string(line.substr(15)));
      state.show_statusline = value == "1" || value == "true" ||
                              value == "on" || value == "yes";
    } else if (StartsWith(line, "shader=")) {
      state.shader_enabled = ParseBoolSetting(line.substr(7), state.shader_enabled);
    }
  }

  if (!state.tabs.empty() && state.active_index >= state.tabs.size()) {
    state.active_index = state.tabs.size() - 1;
  }
  if (state.open_history.size() > kMaxOpenHistoryEntries) {
    state.open_history.erase(
        state.open_history.begin(),
        state.open_history.end() - static_cast<std::ptrdiff_t>(kMaxOpenHistoryEntries));
  }
  for (auto& [engine, history] : state.search_history) {
    if (history.size() > kMaxOpenHistoryEntries) {
      history.erase(
          history.begin(),
          history.end() - static_cast<std::ptrdiff_t>(kMaxOpenHistoryEntries));
    }
  }
  return state;
}

void WriteAppState(const std::string& state_path, const AppState& state) {
  if (state_path.empty()) {
    return;
  }
  const std::filesystem::path path(state_path);
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    return;
  }

  const std::filesystem::path tmp = path.string() + ".tmp";
  {
    std::ofstream file(tmp, std::ios::trunc);
    if (!file) {
      return;
    }
    file << "showmode=" << (state.show_mode_indicator ? "on" : "off") << '\n';
    file << "showfps=" << (state.show_fps_indicator ? "on" : "off") << '\n';
    file << "showstatusline=" << (state.show_statusline ? "on" : "off") << '\n';
    file << "shader=" << (state.shader_enabled ? "on" : "off") << '\n';
    file << "active=" << state.active_index << '\n';
    for (const std::string& tab : state.tabs) {
      if (!tab.empty()) {
        file << "tab=" << EscapeStateValue(tab) << '\n';
      }
    }
    const size_t history_start = state.open_history.size() > kMaxOpenHistoryEntries
                                     ? state.open_history.size() - kMaxOpenHistoryEntries
                                     : 0;
    for (size_t i = history_start; i < state.open_history.size(); ++i) {
      if (!state.open_history[i].empty()) {
        file << "open_history=" << EscapeStateValue(state.open_history[i]) << '\n';
      }
    }
    for (const SearchEngine& engine : SearchEngines()) {
      const auto it = state.search_history.find(engine.name);
      if (it == state.search_history.end()) {
        continue;
      }
      const std::vector<std::string>& history = it->second;
      const size_t search_history_start =
          history.size() > kMaxOpenHistoryEntries
              ? history.size() - kMaxOpenHistoryEntries
              : 0;
      for (size_t i = search_history_start; i < history.size(); ++i) {
        if (!history[i].empty()) {
          file << "search_history_" << engine.name << "="
               << EscapeStateValue(history[i]) << '\n';
        }
      }
    }
    for (const auto& [origin, mask] : state.media_permission_grants) {
      if (!origin.empty() && mask != 0) {
        file << "media_permission_grant=" << mask << '\t'
             << EscapeStateValue(origin) << '\n';
      }
    }
    for (const auto& [origin, mask] : state.media_permission_denials) {
      if (!origin.empty() && mask != 0) {
        file << "media_permission_deny=" << mask << '\t'
             << EscapeStateValue(origin) << '\n';
      }
    }
  }
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
  }
}

std::string ResolveUrlOrSearch(std::string input) {
  if (input.empty()) {
    return "https://example.com";
  }
  if (LooksLikeLocalPath(input)) {
    return FileUrlForLocalPath(input);
  }
  if (LooksLikeUrl(input)) {
    if (input.find("://") == std::string::npos && !StartsWith(input, "data:") &&
        !StartsWith(input, "about:") && !StartsWith(input, "chrome://")) {
      return "https://" + input;
    }
    return input;
  }
  return "https://www.google.com/search?q=" + PercentEncode(input);
}

std::string DisplayUrl(std::string url) {
  if (StartsWith(url, "https://")) {
    url.erase(0, 8);
  } else if (StartsWith(url, "http://")) {
    url.erase(0, 7);
  } else if (StartsWith(url, "file:///")) {
    url.erase(0, 8);
  }
  if (StartsWith(url, "www.")) {
    url.erase(0, 4);
  }
  if (url.size() > 28) {
    url.resize(27);
    url += "...";
  }
  return url;
}

Config ParseConfig(int argc, char* argv[]) {
  Config config;
  config.cache_path = DefaultInstanceCachePath();
  config.state_path = DefaultStatePath();
  bool dwm_save_disabled = false;
  if (const char* dwm_save_argv = std::getenv("VIMBROWSER_DWM_SAVE_ARGV")) {
    if (*dwm_save_argv && !IsDisabledDwmSaveValue(dwm_save_argv)) {
      config.dwm_save_argv = dwm_save_argv;
    } else {
      dwm_save_disabled = true;
    }
  }
  if (const char* state_path = std::getenv("VIMBROWSER_STATE_PATH");
      state_path && *state_path) {
    config.state_path = AbsolutePath(state_path);
  }
  bool is_subprocess = false;

  for (int i = 1; i < argc; ++i) {
    std::string_view arg(argv[i]);
    if (StartsWith(arg, "--type=")) {
      is_subprocess = true;
    } else if (arg == "--disable-gpu") {
      config.disable_gpu = true;
    } else if (StartsWith(arg, "--remote-debugging-port=")) {
      config.remote_debugging_port =
          std::stoi(ValueAfter(arg, "--remote-debugging-port="));
      config.explicit_remote_debugging_port = true;
    } else if (StartsWith(arg, "--profile-dir=")) {
      ApplyProfileDir(config, ValueAfter(arg, "--profile-dir="));
    } else if (arg == "--profile-dir" && i + 1 < argc) {
      ApplyProfileDir(config, argv[++i]);
    } else if (StartsWith(arg, "--cache-path=")) {
      config.cache_path = AbsolutePath(ValueAfter(arg, "--cache-path="));
      config.explicit_cache_path = true;
    } else if (arg == "--cache-path" && i + 1 < argc) {
      config.cache_path = AbsolutePath(argv[++i]);
      config.explicit_cache_path = true;
    } else if (StartsWith(arg, "--vimbrowser-state-path=")) {
      config.state_path = AbsolutePath(ValueAfter(arg, "--vimbrowser-state-path="));
    } else if (arg == "--vimbrowser-state-path" && i + 1 < argc) {
      config.state_path = AbsolutePath(argv[++i]);
    } else if (StartsWith(arg, "--vimbrowser-shader=")) {
      config.shader_enabled = ParseBoolSetting(
          ValueAfter(arg, "--vimbrowser-shader="), config.shader_enabled);
      config.explicit_shader_enabled = true;
    } else if (arg == "--vimbrowser-shader") {
      config.shader_enabled = true;
      config.explicit_shader_enabled = true;
    } else if (!arg.empty() && arg[0] != '-') {
      const std::string url = ResolveUrlOrSearch(std::string(arg));
      config.initial_urls.push_back(url);
      config.explicit_initial_urls.push_back(url);
    }
  }

  const AppState state = ReadAppState(config.state_path);
  config.show_mode_indicator = state.show_mode_indicator;
  config.show_fps_indicator = state.show_fps_indicator;
  config.show_statusline = state.show_statusline;
  if (!config.explicit_shader_enabled) {
    config.shader_enabled = state.shader_enabled;
  }

  if (!config.explicit_initial_urls.empty()) {
    if (!state.tabs.empty()) {
      config.initial_urls = state.tabs;
      config.initial_urls.insert(config.initial_urls.end(),
                                 config.explicit_initial_urls.begin(),
                                 config.explicit_initial_urls.end());
      config.active_index = config.initial_urls.size() - 1;
    }
    if (!config.initial_urls.empty()) {
      config.initial_url = config.initial_urls[std::min(config.active_index,
                                                        config.initial_urls.size() - 1)];
    }
  } else if (!state.tabs.empty()) {
    config.initial_urls = state.tabs;
    config.active_index = std::min(state.active_index, config.initial_urls.size() - 1);
    config.initial_url = config.initial_urls[config.active_index];
  } else {
    config.initial_urls.push_back(config.initial_url);
  }

  if (!dwm_save_disabled && config.dwm_save_argv.empty()) {
    config.dwm_save_argv = BuildDefaultDwmSaveArgv(config);
  }

  if (!is_subprocess) {
    std::filesystem::create_directories(config.cache_path);
    std::filesystem::create_directories(std::filesystem::path(config.state_path).parent_path());
  }
  return config;
}

}  // namespace vimbrowser
