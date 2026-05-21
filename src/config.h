#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace vimbrowser {

inline constexpr size_t kMaxOpenHistoryEntries = 1000;

struct Config {
  std::string initial_url = "https://example.com";
  std::vector<std::string> initial_urls;
  std::vector<std::string> explicit_initial_urls;
  std::string profile_dir;
  std::string cache_path;
  std::string state_path;
  size_t active_index = 0;
  int remote_debugging_port = 0;
  bool disable_gpu = false;
  bool explicit_profile_dir = false;
  bool explicit_cache_path = false;
  bool explicit_remote_debugging_port = false;
  bool explicit_shader_enabled = false;
  bool show_mode_indicator = true;
  bool show_fps_indicator = false;
  bool shader_enabled = true;
};

struct AppState {
  std::vector<std::string> tabs;
  std::vector<std::string> open_history;
  std::map<std::string, std::vector<std::string>> search_history;
  size_t active_index = 0;
  bool show_mode_indicator = true;
  bool show_fps_indicator = false;
  bool shader_enabled = true;
};

struct SearchEngine {
  std::string name;
  std::string url_template;
};

Config ParseConfig(int argc, char* argv[]);
std::string DefaultStatePath();
AppState ReadAppState(const std::string& state_path);
void WriteAppState(const std::string& state_path, const AppState& state);
std::string ResolveUrlOrSearch(std::string input);
const std::vector<SearchEngine>& SearchEngines();
const SearchEngine* FindSearchEngine(std::string_view name);
std::string ResolveSearchEngineUrl(std::string_view name, std::string_view query);
std::string DisplayUrl(std::string url);

}  // namespace vimbrowser
