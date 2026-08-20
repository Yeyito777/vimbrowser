// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/metrics/motherboard.h"

#include <optional>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"


namespace metrics {
namespace {

struct MotherboardDetails {
  std::optional<std::string> manufacturer;
  std::optional<std::string> model;
  std::optional<std::string> bios_manufacturer;
  std::optional<std::string> bios_version;
  std::optional<Motherboard::BiosType> bios_type;
};

#if BUILDFLAG(IS_LINUX)
using base::FilePath;
using base::PathExists;
using base::ReadFileToString;
using base::TrimWhitespaceASCII;
using base::TRIM_TRAILING;

MotherboardDetails ReadMotherboardDetails() {
  constexpr FilePath::CharType kDmiPath[] = "/sys/devices/virtual/dmi/id";
  constexpr FilePath::CharType kEfiPath[] = "/sys/firmware/efi";
  const FilePath dmi_path(kDmiPath);
  MotherboardDetails details;
  std::string temp;
  if (ReadFileToString(dmi_path.Append("board_vendor"), &temp)) {
    details.manufacturer =
        std::string(TrimWhitespaceASCII(temp, TRIM_TRAILING));
  }
  if (ReadFileToString(dmi_path.Append("board_name"), &temp)) {
    details.model = std::string(TrimWhitespaceASCII(temp, TRIM_TRAILING));
  }
  if (ReadFileToString(dmi_path.Append("bios_vendor"), &temp)) {
    details.bios_manufacturer =
        std::string(TrimWhitespaceASCII(temp, TRIM_TRAILING));
  }
  if (ReadFileToString(dmi_path.Append("bios_version"), &temp)) {
    details.bios_version =
        std::string(TrimWhitespaceASCII(temp, TRIM_TRAILING));
  }
  if (PathExists(FilePath(kEfiPath))) {
    details.bios_type = Motherboard::BiosType::kUefi;
  } else {
    details.bios_type = Motherboard::BiosType::kLegacy;
  }
  return details;
}
#endif

}  // namespace

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_WIN)
Motherboard::Motherboard() {
  auto details = ReadMotherboardDetails();
  manufacturer_ = std::move(details.manufacturer),
  model_ = std::move(details.model),
  bios_manufacturer_ = std::move(details.bios_manufacturer),
  bios_version_ = std::move(details.bios_version),
  bios_type_ = std::move(details.bios_type);
}
#else
Motherboard::Motherboard() = default;
#endif

Motherboard::~Motherboard() = default;

}  // namespace metrics
