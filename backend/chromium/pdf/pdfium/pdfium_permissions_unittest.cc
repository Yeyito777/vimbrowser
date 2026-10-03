// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pdf/pdfium/pdfium_permissions.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace chrome_pdf {

namespace {

constexpr uint32_t GeneratePermissions2(uint32_t permissions) {
  constexpr uint32_t kBasePermissions = 0xffffffc0;
  return kBasePermissions | permissions;
}

constexpr uint32_t GeneratePermissions3(uint32_t permissions) {
  constexpr uint32_t kBasePermissions = 0xfffff0c0;
  return kBasePermissions | permissions;
}

static_assert(kPDFPermissionBit05CopyMask == 0x10, "Wrong permission");
static_assert(kPDFPermissionBit10CopyAccessibleMask == 0x200,
              "Wrong permission");
static_assert(GeneratePermissions2(0) == 0xffffffc0, "Wrong permission");
static_assert(GeneratePermissions2(kPDFPermissionBit05CopyMask) == 0xffffffd0,
              "Wrong permission");
static_assert(GeneratePermissions3(0) == 0xfffff0c0, "Wrong permission");
static_assert(GeneratePermissions3(kPDFPermissionBit05CopyMask |
                                   kPDFPermissionBit10CopyAccessibleMask) ==
                  0xfffff2d0,
              "Wrong permission");

TEST(PDFiumPermissionTest, InvalidSecurityHandler) {
  constexpr uint32_t kNoPermissions = 0;

  auto unknown_permissions =
      PDFiumPermissions::CreateForTesting(-1, kNoPermissions);
  EXPECT_TRUE(unknown_permissions.HasPermission(DocumentPermission::kCopy));
  EXPECT_TRUE(
      unknown_permissions.HasPermission(DocumentPermission::kCopyAccessible));

  auto obsolete_permissions =
      PDFiumPermissions::CreateForTesting(1, kNoPermissions);
  EXPECT_TRUE(obsolete_permissions.HasPermission(DocumentPermission::kCopy));
  EXPECT_TRUE(
      obsolete_permissions.HasPermission(DocumentPermission::kCopyAccessible));
}

TEST(PDFiumPermissionTest, Revision2SecurityHandlerNone) {
  auto permissions =
      PDFiumPermissions::CreateForTesting(2, GeneratePermissions2(0));
  EXPECT_FALSE(permissions.HasPermission(DocumentPermission::kCopy));
  EXPECT_FALSE(permissions.HasPermission(DocumentPermission::kCopyAccessible));
}

TEST(PDFiumPermissionTest, Revision2SecurityHandlerCopy) {
  auto permissions = PDFiumPermissions::CreateForTesting(
      2, GeneratePermissions2(kPDFPermissionBit05CopyMask));
  EXPECT_TRUE(permissions.HasPermission(DocumentPermission::kCopy));
  EXPECT_TRUE(permissions.HasPermission(DocumentPermission::kCopyAccessible));
}

TEST(PDFiumPermissionTest, Revision3SecurityHandlerNone) {
  auto permissions =
      PDFiumPermissions::CreateForTesting(3, GeneratePermissions3(0));
  EXPECT_FALSE(permissions.HasPermission(DocumentPermission::kCopy));
  EXPECT_FALSE(permissions.HasPermission(DocumentPermission::kCopyAccessible));
}

TEST(PDFiumPermissionTest, Revision3SecurityHandlerCopy) {
  auto copy_permissions = PDFiumPermissions::CreateForTesting(
      3, GeneratePermissions3(kPDFPermissionBit05CopyMask));
  EXPECT_TRUE(copy_permissions.HasPermission(DocumentPermission::kCopy));
  EXPECT_FALSE(
      copy_permissions.HasPermission(DocumentPermission::kCopyAccessible));

  auto accessible_permissions = PDFiumPermissions::CreateForTesting(
      3, GeneratePermissions3(kPDFPermissionBit10CopyAccessibleMask));
  EXPECT_FALSE(accessible_permissions.HasPermission(DocumentPermission::kCopy));
  EXPECT_TRUE(accessible_permissions.HasPermission(
      DocumentPermission::kCopyAccessible));

  auto all_copy_permissions = PDFiumPermissions::CreateForTesting(
      3, GeneratePermissions3(kPDFPermissionBit05CopyMask |
                              kPDFPermissionBit10CopyAccessibleMask));
  EXPECT_TRUE(all_copy_permissions.HasPermission(DocumentPermission::kCopy));
  EXPECT_TRUE(all_copy_permissions.HasPermission(
      DocumentPermission::kCopyAccessible));
}

}  // namespace

}  // namespace chrome_pdf
