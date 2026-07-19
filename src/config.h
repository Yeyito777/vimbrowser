#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace vimbrowser {

inline constexpr size_t kMaxOpenHistoryEntries = 1000;

struct Config {
  std::string initial_url = "https://example.com";
  std::vector<std::string> initial_urls;
  std::vector<uint64_t> initial_tab_folder_ids;
  std::vector<uint64_t> initial_tab_sort_orders;
  std::vector<bool> initial_tab_pinned;
  std::vector<std::string> explicit_initial_urls;
  std::string profile_dir;
  std::string cache_path;
  std::string state_path;
  std::string dwm_save_argv;
  size_t active_index = 0;
  int remote_debugging_port = 0;
  bool disable_gpu = false;
  bool explicit_profile_dir = false;
  bool explicit_cache_path = false;
  bool explicit_remote_debugging_port = false;
  bool explicit_shader_enabled = false;
  bool show_mode_indicator = true;
  bool show_fps_indicator = false;
  bool show_statusline = true;
  bool shader_enabled = true;
  bool a26_shell = false;
};

struct SavedSidebarFolder {
  uint64_t id = 0;
  uint64_t parent_id = 0;
  uint64_t sort_order = 0;
  std::string name;
  bool pinned = false;
};

struct AppState {
  std::vector<std::string> tabs;
  // These vectors are kept index-aligned with |tabs|. Zero means the sidebar
  // root for folder ids and "assign a legacy/default order" for sort orders.
  std::vector<uint64_t> tab_folder_ids;
  std::vector<uint64_t> tab_sort_orders;
  std::vector<bool> tab_pinned;
  std::vector<SavedSidebarFolder> sidebar_folders;
  std::vector<std::string> open_history;
  std::map<std::string, std::vector<std::string>> search_history;
  std::map<std::string, uint32_t> media_permission_grants;
  std::map<std::string, uint32_t> media_permission_denials;
  size_t active_index = 0;
  uint64_t sidebar_folder_id = 0;
  uint64_t next_sidebar_folder_id = 1;
  bool show_mode_indicator = true;
  bool show_fps_indicator = false;
  bool show_statusline = true;
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
const std::string& ChatgptAutosendToken();
std::string DisplayUrl(std::string url);

}  // namespace vimbrowser
