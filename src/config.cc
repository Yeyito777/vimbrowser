#include "config.h"

#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <unistd.h>

namespace vimbrowser {
namespace {

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
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

std::string DefaultCachePath() {
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
    return std::string(xdg) + "/vimbrowser/cef";
  }
  if (const char* home = std::getenv("HOME"); home && *home) {
    return std::string(home) + "/.cache/vimbrowser/cef";
  }
  return "/tmp/vimbrowser-cef-cache";
}

std::string DefaultInstanceCachePath() {
  return DefaultCachePath() + "/instances/" + std::to_string(getpid());
}

std::string ValueAfter(std::string_view arg, std::string_view prefix) {
  return std::string(arg.substr(prefix.size()));
}

}  // namespace

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
    } else if (StartsWith(arg, "--cache-path=")) {
      config.cache_path = ValueAfter(arg, "--cache-path=");
      config.explicit_cache_path = true;
    } else if (!arg.empty() && arg[0] != '-') {
      config.initial_url = ResolveUrlOrSearch(std::string(arg));
    }
  }

  if (!is_subprocess) {
    std::filesystem::create_directories(config.cache_path);
  }
  return config;
}

}  // namespace vimbrowser
