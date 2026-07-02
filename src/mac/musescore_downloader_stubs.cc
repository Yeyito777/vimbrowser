#include "musescore_downloader.h"

#include <cstdlib>
#include <utility>

namespace vimbrowser {

const std::string& MuseScoreMetadataScript() {
  static const std::string script =
      "(()=>{throw new Error('MuseScore PDF export is not available in this "
      "macOS build')})()";
  return script;
}

std::filesystem::path DefaultMuseScoreDownloadDirectory() {
  if (const char* home = std::getenv("HOME"); home && *home) {
    return std::filesystem::path(home) / "Downloads";
  }
  return std::filesystem::current_path();
}

MuseScorePdfResult DownloadMuseScorePdf(
    const std::string&,
    const std::vector<std::string>&,
    const std::filesystem::path&,
    const MuseScoreProgressCallback&) {
  return {.error =
              "MuseScore PDF export is not available in this macOS build"};
}

}  // namespace vimbrowser
