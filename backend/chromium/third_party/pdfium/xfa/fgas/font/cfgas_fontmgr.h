// Copyright 2014 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef XFA_FGAS_FONT_CFGAS_FONTMGR_H_
#define XFA_FGAS_FONT_CFGAS_FONTMGR_H_

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "build/build_config.h"
#include "core/fxcrt/cfx_read_only_container_stream.h"
#include "core/fxcrt/compiler_specific.h"
#include "core/fxcrt/fx_codepage_forward.h"
#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/widestring.h"
#include "core/fxge/cfx_face.h"

class CFGAS_GEFont;


// Represents metatdata about a font that isn't necessarily loaded yet.
class CFGAS_FontDescriptor {
 public:
  struct Rank {
   public:
    UNOWNED_PTR_EXCLUSION CFGAS_FontDescriptor* font;  // POD struct.
    int32_t penalty;

    friend auto operator<=>(const Rank& lhs, const Rank& rhs) {
      return lhs.penalty <=> rhs.penalty;
    }
  };

  CFGAS_FontDescriptor();
  ~CFGAS_FontDescriptor();

  bool VerifyUnicode(wchar_t unicode);

  int32_t face_index_ = 0;
  uint32_t font_styles_ = 0;
  WideString face_name_;
  RetainPtr<CFX_Face> face_;  // May be null until required.
  std::vector<WideString> family_names_;
  std::array<uint32_t, 4> usb_ = {};
  std::array<uint32_t, 2> csb_ = {};
};


class CFGAS_FontMgr {
 public:
  CFGAS_FontMgr();
  ~CFGAS_FontMgr();

  bool EnumFonts();
  RetainPtr<CFGAS_GEFont> GetFontByCodePage(FX_CodePage wCodePage,
                                            uint32_t dwFontStyles,
                                            const wchar_t* pszFontFamily);
  RetainPtr<CFGAS_GEFont> GetFontByUnicode(wchar_t wUnicode,
                                           uint32_t dwFontStyles,
                                           const wchar_t* pszFontFamily);
  RetainPtr<CFGAS_GEFont> LoadFont(const wchar_t* pszFontFamily,
                                   uint32_t dwFontStyles,
                                   FX_CodePage wCodePage);

 private:
  friend class CFGASFontMgr_LazyEnumeration_Test;

  RetainPtr<CFGAS_GEFont> GetFontByUnicodeImpl(wchar_t wUnicode,
                                               uint32_t dwFontStyles,
                                               const wchar_t* pszFontFamily,
                                               uint32_t dwHash,
                                               FX_CodePage wCodePage,
                                               uint16_t wBitField);

  bool EnumFontsFromFontMapper();
  void RegisterFace(RetainPtr<CFX_Face> face,
                    int face_index,
                    const WideString& face_name);
  void RegisterFaces(
      const RetainPtr<CFX_ReadOnlyFixedSizeDataVectorStream>& font_stream,
      const WideString& face_name);
  std::vector<CFGAS_FontDescriptor::Rank> MatchFonts(FX_CodePage wCodePage,
                                                     uint32_t dwFontStyles,
                                                     const WideString& FontName,
                                                     wchar_t wcUnicode);
  RetainPtr<CFGAS_GEFont> LoadFontInternal(const WideString& face_name,
                                           int32_t face_index);
  void EnsureFontsEnumerated();

  std::map<uint32_t, std::vector<RetainPtr<CFGAS_GEFont>>> hash_2fonts_;
  std::set<wchar_t> failed_unicodes_set_;

  bool fonts_enumerated_ = false;
  std::vector<std::unique_ptr<CFGAS_FontDescriptor>> installed_fonts_;
  std::map<uint32_t, std::vector<CFGAS_FontDescriptor::Rank>>
      hash_to_candidates_map_;
};

#endif  // XFA_FGAS_FONT_CFGAS_FONTMGR_H_
