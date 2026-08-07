#include "config.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_set>
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

std::string HexEncodeBytes(const unsigned char* bytes, size_t size) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(size * 2);
  for (size_t i = 0; i < size; ++i) {
    out.push_back(kHex[(bytes[i] >> 4) & 0xF]);
    out.push_back(kHex[bytes[i] & 0xF]);
  }
  return out;
}

std::string GenerateChatgptAutosendToken() {
  unsigned char bytes[16] = {};
  std::ifstream urandom("/dev/urandom", std::ios::binary);
  if (urandom.read(reinterpret_cast<char*>(bytes), sizeof(bytes))) {
    return HexEncodeBytes(bytes, sizeof(bytes));
  }

  // Extremely defensive fallback for unusual systems without /dev/urandom.  The
  // token is only used to keep arbitrary ChatGPT links from triggering the local
  // auto-submit automation path.
  std::ostringstream fallback;
  fallback << std::hex << static_cast<unsigned long long>(::getpid())
           << static_cast<unsigned long long>(
                  std::chrono::steady_clock::now().time_since_epoch().count());
  return fallback.str();
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

[[maybe_unused]] std::string DefaultInstanceCachePath() {
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
    } else if (c == '\t') {
      out += "\\t";
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
    } else if (escaped == 't') {
      out.push_back('\t');
    } else {
      out.push_back(escaped);
    }
  }
  return out;
}

bool ParseStateUint64(std::string_view text, uint64_t* value) {
  if (!value || text.empty()) {
    return false;
  }
  const char* begin = text.data();
  const char* end = begin + text.size();
  uint64_t parsed = 0;
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc() || result.ptr != end) {
    return false;
  }
  *value = parsed;
  return true;
}

void ReadSidebarFolder(std::string_view payload,
                       std::vector<SavedSidebarFolder>* folders) {
  if (!folders) {
    return;
  }
  const size_t first_tab = payload.find('\t');
  const size_t second_tab = first_tab == std::string_view::npos
                                ? std::string_view::npos
                                : payload.find('\t', first_tab + 1);
  const size_t third_tab = second_tab == std::string_view::npos
                               ? std::string_view::npos
                               : payload.find('\t', second_tab + 1);
  if (first_tab == std::string_view::npos ||
      second_tab == std::string_view::npos ||
      third_tab == std::string_view::npos) {
    return;
  }

  SavedSidebarFolder folder;
  if (!ParseStateUint64(payload.substr(0, first_tab), &folder.id) ||
      folder.id == 0 ||
      !ParseStateUint64(payload.substr(first_tab + 1,
                                       second_tab - first_tab - 1),
                        &folder.parent_id) ||
      !ParseStateUint64(payload.substr(second_tab + 1,
                                       third_tab - second_tab - 1),
                        &folder.sort_order)) {
    return;
  }
  folder.name = UnescapeStateValue(payload.substr(third_tab + 1));
  if (!folder.name.empty()) {
    folders->push_back(std::move(folder));
  }
}

void ReadPinnedSidebarFolder(std::string_view payload,
                             std::vector<SavedSidebarFolder>* folders) {
  uint64_t folder_id = 0;
  if (!folders || !ParseStateUint64(payload, &folder_id) || folder_id == 0) {
    return;
  }
  const auto folder =
      std::find_if(folders->begin(), folders->end(),
                   [&](const SavedSidebarFolder& candidate) {
                     return candidate.id == folder_id;
                   });
  if (folder != folders->end()) {
    folder->pinned = true;
  }
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
      {"ai", "https://chatgpt.com/?vimbrowser_prompt={}"
             "&vimbrowser_autosend={vimbrowser_autosend_token}"},
  };
  return engines;
}

const std::string& ChatgptAutosendToken() {
  static const std::string token = GenerateChatgptAutosendToken();
  return token;
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
  constexpr std::string_view kChatgptTokenPlaceholder =
      "{vimbrowser_autosend_token}";
  const size_t token_placeholder = url.find(kChatgptTokenPlaceholder);
  if (token_placeholder != std::string::npos) {
    url.replace(token_placeholder, kChatgptTokenPlaceholder.size(),
                ChatgptAutosendToken());
  }
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
        state.tab_ids.push_back(0);
        state.tab_folder_ids.push_back(0);
        state.tab_sort_orders.push_back(0);
        state.tab_pinned.push_back(false);
      }
    } else if (StartsWith(line, "tab_id=") && !state.tab_ids.empty()) {
      uint64_t tab_id = 0;
      if (ParseStateUint64(std::string_view(line).substr(7), &tab_id) &&
          tab_id != 0) {
        state.tab_ids.back() = tab_id;
      }
    } else if (StartsWith(line, "tab_folder=") &&
               !state.tab_folder_ids.empty()) {
      uint64_t folder_id = 0;
      if (ParseStateUint64(std::string_view(line).substr(11), &folder_id)) {
        state.tab_folder_ids.back() = folder_id;
      }
    } else if (StartsWith(line, "tab_order=") &&
               !state.tab_sort_orders.empty()) {
      uint64_t sort_order = 0;
      if (ParseStateUint64(std::string_view(line).substr(10), &sort_order)) {
        state.tab_sort_orders.back() = sort_order;
      }
    } else if (StartsWith(line, "tab_pinned=") &&
               !state.tab_pinned.empty()) {
      state.tab_pinned.back() =
          ParseBoolSetting(line.substr(11), state.tab_pinned.back());
    } else if (StartsWith(line, "folder=")) {
      ReadSidebarFolder(std::string_view(line).substr(7),
                        &state.sidebar_folders);
    } else if (StartsWith(line, "folder_pinned=")) {
      ReadPinnedSidebarFolder(std::string_view(line).substr(14),
                              &state.sidebar_folders);
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
    } else if (StartsWith(line, "next_tab_id=")) {
      ParseStateUint64(std::string_view(line).substr(12), &state.next_tab_id);
    } else if (StartsWith(line, "sidebar_folder=")) {
      ParseStateUint64(std::string_view(line).substr(15),
                       &state.sidebar_folder_id);
    } else if (StartsWith(line, "next_sidebar_folder_id=")) {
      ParseStateUint64(std::string_view(line).substr(23),
                       &state.next_sidebar_folder_id);
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
  // A malformed state file must never give two restored tabs the same identity.
  // Keep the first occurrence and let the normal persistent allocator replace
  // later duplicates. Also repair stale nonzero allocator values from older or
  // manually edited state files so newly created tabs cannot collide.
  std::unordered_set<uint64_t> tab_ids;
  uint64_t largest_tab_id = 0;
  for (uint64_t& tab_id : state.tab_ids) {
    if (tab_id == 0 || !tab_ids.insert(tab_id).second) {
      tab_id = 0;
      continue;
    }
    largest_tab_id = std::max(largest_tab_id, tab_id);
  }
  if (state.next_tab_id != 0 && state.next_tab_id <= largest_tab_id) {
    state.next_tab_id =
        largest_tab_id == std::numeric_limits<uint64_t>::max()
            ? 0
            : largest_tab_id + 1;
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
    file << "next_tab_id=" << state.next_tab_id << '\n';
    file << "sidebar_folder=" << state.sidebar_folder_id << '\n';
    file << "next_sidebar_folder_id=" << state.next_sidebar_folder_id << '\n';
    for (const SavedSidebarFolder& folder : state.sidebar_folders) {
      if (folder.id != 0 && !folder.name.empty()) {
        file << "folder=" << folder.id << '\t' << folder.parent_id << '\t'
             << folder.sort_order << '\t' << EscapeStateValue(folder.name)
             << '\n';
        if (folder.pinned) {
          file << "folder_pinned=" << folder.id << '\n';
        }
      }
    }
    for (size_t i = 0; i < state.tabs.size(); ++i) {
      const std::string& tab = state.tabs[i];
      if (!tab.empty()) {
        file << "tab=" << EscapeStateValue(tab) << '\n';
        const uint64_t tab_id =
            i < state.tab_ids.size() ? state.tab_ids[i] : 0;
        if (tab_id != 0) {
          file << "tab_id=" << tab_id << '\n';
        }
        const uint64_t folder_id = i < state.tab_folder_ids.size()
                                       ? state.tab_folder_ids[i]
                                       : 0;
        const uint64_t sort_order = i < state.tab_sort_orders.size()
                                        ? state.tab_sort_orders[i]
                                        : 0;
        if (folder_id != 0) {
          file << "tab_folder=" << folder_id << '\n';
        }
        if (sort_order != 0) {
          file << "tab_order=" << sort_order << '\n';
        }
        if (i < state.tab_pinned.size() && state.tab_pinned[i]) {
          file << "tab_pinned=on\n";
        }
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
#if defined(VIMBROWSER_DEFAULT_PROFILE_DIR)
  ApplyProfileDir(config, VIMBROWSER_DEFAULT_PROFILE_DIR);
#else
  config.cache_path = DefaultInstanceCachePath();
  config.state_path = DefaultStatePath();
#endif
  bool dwm_save_disabled = false;
  if (const char* dwm_save_argv = std::getenv("VIMBROWSER_DWM_SAVE_ARGV")) {
    if (*dwm_save_argv && !IsDisabledDwmSaveValue(dwm_save_argv)) {
      config.dwm_save_argv = dwm_save_argv;
    } else {
      dwm_save_disabled = true;
    }
  }
  if (const char* profile_dir = std::getenv("VIMBROWSER_PROFILE_DIR");
      profile_dir && *profile_dir) {
    ApplyProfileDir(config, profile_dir);
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
    } else if (arg == "--a26-shell") {
      config.a26_shell = true;
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
      config.initial_tab_ids.push_back(0);
      config.initial_tab_folder_ids.push_back(0);
      config.initial_tab_sort_orders.push_back(0);
      config.initial_tab_pinned.push_back(false);
      config.explicit_initial_urls.push_back(url);
    }
  }

  const AppState state = ReadAppState(config.state_path);
  config.next_tab_id = state.next_tab_id;
  config.show_mode_indicator = state.show_mode_indicator;
  config.show_fps_indicator = state.show_fps_indicator;
  config.show_statusline = state.show_statusline;
  if (!config.explicit_shader_enabled) {
    config.shader_enabled = state.shader_enabled;
  }

  if (config.a26_shell) {
    // The phone shell owns system status, app switching and the bottom-edge
    // close gesture.  Start with a page-only surface and software rendering;
    // browser controls can still be exercised through the existing IPC while
    // touch behavior is being measured.
    config.disable_gpu = true;
    config.show_mode_indicator = false;
    config.show_fps_indicator = false;
    config.show_statusline = false;
  }

  if (!config.explicit_initial_urls.empty()) {
    if (!state.tabs.empty()) {
      config.initial_urls = state.tabs;
      config.initial_tab_ids = state.tab_ids;
      config.initial_tab_folder_ids = state.tab_folder_ids;
      config.initial_tab_sort_orders = state.tab_sort_orders;
      config.initial_tab_pinned = state.tab_pinned;
      config.initial_urls.insert(config.initial_urls.end(),
                                 config.explicit_initial_urls.begin(),
                                 config.explicit_initial_urls.end());
      config.initial_tab_ids.resize(config.initial_urls.size(), 0);
      config.initial_tab_folder_ids.resize(config.initial_urls.size(), 0);
      config.initial_tab_sort_orders.resize(config.initial_urls.size(), 0);
      config.initial_tab_pinned.resize(config.initial_urls.size(), false);
      config.active_index = config.initial_urls.size() - 1;
    }
    if (!config.initial_urls.empty()) {
      config.initial_url = config.initial_urls[std::min(config.active_index,
                                                        config.initial_urls.size() - 1)];
    }
  } else if (!state.tabs.empty()) {
    config.initial_urls = state.tabs;
    config.initial_tab_ids = state.tab_ids;
    config.initial_tab_folder_ids = state.tab_folder_ids;
    config.initial_tab_sort_orders = state.tab_sort_orders;
    config.initial_tab_pinned = state.tab_pinned;
    config.active_index = std::min(state.active_index, config.initial_urls.size() - 1);
    config.initial_url = config.initial_urls[config.active_index];
  } else {
    config.initial_urls.push_back(config.initial_url);
    config.initial_tab_ids.push_back(0);
    config.initial_tab_folder_ids.push_back(0);
    config.initial_tab_sort_orders.push_back(0);
    config.initial_tab_pinned.push_back(false);
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
