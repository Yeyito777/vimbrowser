// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_COMPOSITOR_TEST_TEST_SUITE_H_
#define UI_COMPOSITOR_TEST_TEST_SUITE_H_

#include "base/test/test_suite.h"
#include "build/build_config.h"


namespace ui {
namespace test {

class CompositorTestSuite : public base::TestSuite {
 public:
  CompositorTestSuite(int argc, char** argv);

  CompositorTestSuite(const CompositorTestSuite&) = delete;
  CompositorTestSuite& operator=(const CompositorTestSuite&) = delete;

  ~CompositorTestSuite() override;

 protected:
  // base::TestSuite:
  void Initialize() override;

 private:
};

}  // namespace test
}  // namespace ui

#endif  // UI_COMPOSITOR_TEST_TEST_SUITE_H_
