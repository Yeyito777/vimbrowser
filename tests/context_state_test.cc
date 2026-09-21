#include "config.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <unistd.h>

int main() {
  char directory[] = "/tmp/vb-context-state-XXXXXX";
  assert(mkdtemp(directory));
  const std::string path = std::string(directory) + "/state";
  vimbrowser::AppState state;
  state.tabs = {"https://default.test/", "https://isolated.test/?q=a\tb\nc",
                "https://default.test/last"};
  state.tab_contexts = {"", "work-1", ""};
  state.tab_folder_ids = {0, 7, 0};
  state.tab_sort_orders = {1024, 2048, 3072};
  state.tab_pinned = {false, true, false};
  state.active_index = 1;
  vimbrowser::WriteAppState(path, state);
  auto restored = vimbrowser::ReadAppState(path);
  assert(restored.tabs == state.tabs);
  assert(restored.tab_contexts == state.tab_contexts);
  assert(restored.tab_folder_ids == state.tab_folder_ids);
  assert(restored.tab_sort_orders == state.tab_sort_orders);
  assert(restored.tab_pinned == state.tab_pinned);
  assert(restored.active_index == 1);
  std::ifstream input(path);
  std::string text((std::istreambuf_iterator<char>(input)), {});
  assert(text.find("context_tab=work-1\t0\t7\t2048\ton\t") != std::string::npos);
  assert(text.find("\ntab=https://isolated") == std::string::npos);
  // Independent records prevent downgrade leaks into the default profile.
  std::ofstream malformed(path, std::ios::trunc);
  malformed << "tab=https://legacy.test/\n"
               "context_tab=../escape\t0\t0\t0\toff\thttps://bad.test/\n"
               "context_tab=valid\t0\t0\t0\tinvalid\thttps://bad.test/\n"
               "context_tab=valid\t0\t0\t0\toff\thttps://good.test/\n"
               "context_tab=-bad\t0\t0\t0\toff\thttps://bad.test/\n"
               "tab=https://legacy.test/last\n";
  malformed.close();
  restored = vimbrowser::ReadAppState(path);
  assert(restored.tabs.size() == 3);
  assert((restored.tab_contexts == std::vector<std::string>{"", "valid", ""}));
  assert(restored.tabs[1] == "https://good.test/");
  std::filesystem::remove_all(directory);
  std::cout << "PASS context roundtrip, escaping, isolation, legacy and invalid records\n";
}
