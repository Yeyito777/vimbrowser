#pragma once

#include <string>

namespace vimbrowser {

struct Config {
  std::string initial_url = "https://example.com";
  std::string cache_path;
  int remote_debugging_port = 9222;
  bool disable_gpu = false;
};

Config ParseConfig(int argc, char* argv[]);
std::string ResolveUrlOrSearch(std::string input);

}  // namespace vimbrowser
