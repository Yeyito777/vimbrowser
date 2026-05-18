#include "config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>
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

bool LooksLikeUrl(std::string_view text) {
  return StartsWith(text, "http://") || StartsWith(text, "https://") ||
         StartsWith(text, "file://") || StartsWith(text, "data:") ||
         StartsWith(text, "about:") || StartsWith(text, "chrome://") ||
         text.find('.') != std::string_view::npos || StartsWith(text, "localhost");
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
      config.initial_urls.push_back(ResolveUrlOrSearch(std::string(arg)));
    }
  }

  const AppState state = ReadAppState(config.state_path);
  config.show_mode_indicator = state.show_mode_indicator;
  config.show_fps_indicator = state.show_fps_indicator;
  if (!config.explicit_shader_enabled) {
    config.shader_enabled = state.shader_enabled;
  }

  if (!config.initial_urls.empty()) {
    config.initial_url = config.initial_urls.front();
  } else if (!state.tabs.empty()) {
    config.initial_urls = state.tabs;
    config.active_index = std::min(state.active_index, config.initial_urls.size() - 1);
    config.initial_url = config.initial_urls[config.active_index];
  } else {
    config.initial_urls.push_back(config.initial_url);
  }

  if (!is_subprocess) {
    std::filesystem::create_directories(config.cache_path);
    std::filesystem::create_directories(std::filesystem::path(config.state_path).parent_path());
  }
  return config;
}

}  // namespace vimbrowser
