#pragma once

#include <string>
#include <vector>

namespace vimbrowser {

struct Config {
  std::string initial_url = "https://example.com";
  std::vector<std::string> initial_urls;
  std::string cache_path;
  std::string state_path;
  size_t active_index = 0;
  int remote_debugging_port = 0;
  bool disable_gpu = false;
  bool explicit_cache_path = false;
  bool explicit_remote_debugging_port = false;
  bool show_mode_indicator = true;
};

struct AppState {
  std::vector<std::string> tabs;
  size_t active_index = 0;
  bool show_mode_indicator = true;
};

Config ParseConfig(int argc, char* argv[]);
std::string DefaultStatePath();
AppState ReadAppState(const std::string& state_path);
void WriteAppState(const std::string& state_path, const AppState& state);
std::string ResolveUrlOrSearch(std::string input);
std::string DisplayUrl(std::string url);

}  // namespace vimbrowser
