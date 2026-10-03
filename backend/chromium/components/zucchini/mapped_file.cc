// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/zucchini/mapped_file.h"

#include <utility>

#include "base/files/file_util.h"
#include "build/build_config.h"

namespace zucchini {

MappedFileReader::MappedFileReader(base::File file) {
  if (!file.IsValid()) {
    error_ = "Invalid file.";
    return;  // |buffer_| will be uninitialized, and therefore invalid.
  }
  if (!buffer_.Initialize(std::move(file))) {
    error_ = "Can't map file to memory.";
  }
}

MappedFileWriter::MappedFileWriter(const base::FilePath& file_path,
                                   base::File file,
                                   size_t length)
    : file_path_(file_path), delete_behavior_(kManualDeleteOnClose) {
  if (!file.IsValid()) {
    error_ = "Invalid file.";
    return;  // |buffer_| will be uninitialized, and therefore invalid.
  }


  bool is_ok = buffer_.Initialize(std::move(file), {0, length},
                                  base::MemoryMappedFile::READ_WRITE_EXTEND);
  if (!is_ok) {
    error_ = "Can't map file to memory.";
  }
}

MappedFileWriter::~MappedFileWriter() {
  if (!HasError() && delete_behavior_ == kManualDeleteOnClose &&
      !file_path_.empty() && !base::DeleteFile(file_path_)) {
    error_ = "Failed to delete file.";
  }
}

bool MappedFileWriter::Keep() {
  delete_behavior_ = kKeep;
  return true;
}

}  // namespace zucchini
