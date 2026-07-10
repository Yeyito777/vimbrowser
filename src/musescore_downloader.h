#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace vimbrowser {

using MuseScoreProgressCallback = std::function<void(std::string)>;

struct MuseScorePdfResult {
  std::string output_path;
  std::string error;
};

const std::string& MuseScoreMetadataScript();
std::filesystem::path DefaultMuseScoreDownloadDirectory();
MuseScorePdfResult DownloadMuseScorePdf(
    const std::string& title,
    const std::vector<std::string>& urls,
    const std::filesystem::path& download_directory,
    const MuseScoreProgressCallback& progress = {});

}  // namespace vimbrowser
