#include "config.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void ExpectEqual(const std::vector<uint64_t>& actual,
                 const std::vector<uint64_t>& expected,
                 const std::string& message) {
  Expect(actual == expected, message);
}

void TestPersistentTabIdRoundTrip(const std::filesystem::path& state_path) {
  vimbrowser::AppState written;
  written.tabs = {"https://three.example/", "https://one.example/",
                  "https://two.example/"};
  // Deliberately use nonsequential ids in a different order. This is the shape
  // produced after tabs have been closed and reordered before a restart.
  written.tab_ids = {30, 10, 20};
  written.tab_folder_ids = {3, 1, 2};
  written.tab_sort_orders = {300, 100, 200};
  written.tab_pinned = {true, false, true};
  written.tab_contexts = {"", "fixture-a", "fixture-b"};
  written.active_index = 1;
  // The allocator can be higher than every live tab after higher-id tabs close.
  written.next_tab_id = 91;

  vimbrowser::WriteAppState(state_path.string(), written);
  const vimbrowser::AppState read =
      vimbrowser::ReadAppState(state_path.string());

  Expect(read.tabs == written.tabs, "tab URLs changed during state round trip");
  Expect(read.tab_contexts == written.tab_contexts, "tab contexts changed during round trip");
  ExpectEqual(read.tab_ids, written.tab_ids,
              "tab IDs changed during state round trip");
  Expect(read.tab_folder_ids == written.tab_folder_ids,
         "tab folders changed during state round trip");
  Expect(read.tab_sort_orders == written.tab_sort_orders,
         "tab order changed during state round trip");
  Expect(read.tab_pinned == written.tab_pinned,
         "tab pin state changed during state round trip");
  Expect(read.active_index == written.active_index,
         "active tab changed during state round trip");
  Expect(read.next_tab_id == written.next_tab_id,
         "persistent tab allocator changed during state round trip");
}

void TestConfigRestoresIdsAndAllocatesExplicitUrls(
    const std::filesystem::path& state_path,
    const std::filesystem::path& cache_path) {
  std::vector<std::string> arguments = {
      "vimbrowser-config-state-test", "--cache-path", cache_path.string(),
      "--vimbrowser-state-path", state_path.string(),
      "https://new.example/"};
  std::vector<char*> argv;
  argv.reserve(arguments.size());
  for (std::string& argument : arguments) {
    argv.push_back(argument.data());
  }

  const vimbrowser::Config config =
      vimbrowser::ParseConfig(static_cast<int>(argv.size()), argv.data());
  Expect(config.initial_urls.size() == 4,
         "explicit URL was not appended to restored tabs");
  ExpectEqual(config.initial_tab_ids, {30, 10, 20, 0},
              "config did not preserve restored IDs and reserve a fresh one");
  Expect(config.initial_tab_contexts == std::vector<std::string>({"", "fixture-a", "fixture-b", ""}),
         "config lost restored context identity or misassigned explicit URL context");
  Expect(config.next_tab_id == 91,
         "config did not restore the persistent tab allocator");
}

void TestLegacyStateMigration(const std::filesystem::path& state_path) {
  {
    std::ofstream file(state_path, std::ios::trunc);
    file << "active=1\n"
         << "tab=https://legacy-one.example/\n"
         << "tab=https://legacy-two.example/\n";
  }

  const vimbrowser::AppState read =
      vimbrowser::ReadAppState(state_path.string());
  ExpectEqual(read.tab_ids, {0, 0},
              "legacy tabs should request fresh persistent IDs");
  Expect(read.next_tab_id == 1,
         "legacy state should start the persistent allocator at one");
}

void TestMalformedIdsAreRepaired(const std::filesystem::path& state_path) {
  {
    std::ofstream file(state_path, std::ios::trunc);
    file << "next_tab_id=2\n"
         << "tab=https://first.example/\n"
         << "tab_id=9\n"
         << "tab=https://duplicate.example/\n"
         << "tab_id=9\n"
         << "tab=https://third.example/\n"
         << "tab_id=3\n";
  }

  const vimbrowser::AppState read =
      vimbrowser::ReadAppState(state_path.string());
  ExpectEqual(read.tab_ids, {9, 0, 3},
              "duplicate tab ID was not rejected deterministically");
  Expect(read.next_tab_id == 10,
         "stale allocator was not advanced beyond restored IDs");
}

void TestAllocatorExhaustionSentinel(const std::filesystem::path& state_path) {
  {
    std::ofstream file(state_path, std::ios::trunc);
    file << "next_tab_id=1\n"
         << "tab=https://last.example/\n"
         << "tab_id=" << std::numeric_limits<uint64_t>::max() << '\n';
  }

  const vimbrowser::AppState read =
      vimbrowser::ReadAppState(state_path.string());
  Expect(read.next_tab_id == 0,
         "uint64_t exhaustion must use the zero allocator sentinel");
}

void TestMoreThan200MixedContexts(const std::filesystem::path& path) {
  vimbrowser::AppState written;
  for (uint64_t i = 0; i < 223; ++i) {
    written.tabs.push_back("https://fixture.invalid/" + std::to_string(i));
    written.tab_ids.push_back(400 + i);
    written.tab_contexts.push_back(i % 10 == 0 ? "fixture-isolated" : "");
    written.tab_folder_ids.push_back(0);
    written.tab_sort_orders.push_back(i * 1024 + 1);
    written.tab_pinned.push_back(i % 2 == 0);
  }
  written.active_index = 222;
  written.next_tab_id = 700;
  vimbrowser::WriteAppState(path.string(), written);
  const auto read = vimbrowser::ReadAppState(path.string());
  Expect(read.tabs == written.tabs && read.tab_ids == written.tab_ids &&
         read.tab_contexts == written.tab_contexts && read.tab_pinned == written.tab_pinned &&
         read.tab_sort_orders == written.tab_sort_orders && read.active_index == 222 && read.next_tab_id == 700,
         "223 mixed-context tabs failed exact round trip");
  std::ofstream file(path, std::ios::trunc);
  file << "tab=https://safe.invalid/\ntab_id=1\ncontext_tab=../unsafe\t2\t0\t0\toff\thttps://isolated.invalid/\n";
  file.close();
  const auto invalid = vimbrowser::ReadAppState(path.string());
  Expect(invalid.tabs.size() == 1 && invalid.tab_contexts[0].empty(),
         "malformed context record must not fall back to global context");
}

}  // namespace

int main() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("vimbrowser-config-state-test-" + std::to_string(getpid()));
  const std::filesystem::path state_path = root / "state";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root);

  try {
    TestPersistentTabIdRoundTrip(state_path);
    TestConfigRestoresIdsAndAllocatesExplicitUrls(state_path, root / "cache");
    TestLegacyStateMigration(state_path);
    TestMalformedIdsAreRepaired(state_path);
    TestAllocatorExhaustionSentinel(state_path);
    TestMoreThan200MixedContexts(state_path);
  } catch (const std::exception& error) {
    std::cerr << "vimbrowser config state test failed: " << error.what()
              << '\n';
    std::filesystem::remove_all(root, ec);
    return 1;
  }

  std::filesystem::remove_all(root, ec);
  return 0;
}
