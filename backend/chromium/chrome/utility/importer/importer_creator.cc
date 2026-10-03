// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/utility/importer/importer_creator.h"

#include "base/notreached.h"
#include "build/build_config.h"
#include "chrome/utility/importer/bookmarks_file_importer.h"
#include "chrome/utility/importer/firefox_importer.h"


#if BUILDFLAG(IS_MAC)
#include "base/apple/foundation_util.h"
#include "chrome/utility/importer/safari_importer.h"
#endif

namespace importer {

scoped_refptr<Importer> CreateImporterByType(
    user_data_importer::ImporterType type) {
  switch (type) {
    case user_data_importer::TYPE_BOOKMARKS_FILE:
      return new BookmarksFileImporter();
    case user_data_importer::TYPE_FIREFOX:
      return new FirefoxImporter();
#if BUILDFLAG(IS_MAC)
    case user_data_importer::TYPE_SAFARI:
      return new SafariImporter(base::apple::GetUserLibraryPath());
#endif
    default:
      NOTREACHED();
  }
}

}  // namespace importer
