#pragma once

#include <string>

namespace vimbrowser {

struct Config {
  std::string initial_url = "https://example.com";
  std::string cache_path;
  int remote_debugging_port = 0;
  bool disable_gpu = false;
  bool explicit_cache_path = false;
  bool explicit_remote_debugging_port = false;
};

Config ParseConfig(int argc, char* argv[]);
std::string ResolveUrlOrSearch(std::string input);
std::string DisplayUrl(std::string url);

}  // namespace vimbrowser
