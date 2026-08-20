// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FPDFAPI_RENDER_CPDF_DOCRENDERDATA_H_
#define CORE_FPDFAPI_RENDER_CPDF_DOCRENDERDATA_H_

#include <functional>
#include <map>

#include "build/build_config.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fxcrt/observed_ptr.h"
#include "core/fxcrt/retain_ptr.h"


class CPDF_Font;
class CPDF_Object;
class CPDF_TransferFunc;
class CPDF_Type3Cache;
class CPDF_Type3Font;


class CPDF_DocRenderData : public CPDF_Document::RenderDataIface {
 public:
  static CPDF_DocRenderData* FromDocument(const CPDF_Document* doc);

  CPDF_DocRenderData();
  ~CPDF_DocRenderData() override;

  CPDF_DocRenderData(const CPDF_DocRenderData&) = delete;
  CPDF_DocRenderData& operator=(const CPDF_DocRenderData&) = delete;

  // The argument to these methods must be non-null.
  RetainPtr<CPDF_Type3Cache> GetCachedType3(CPDF_Type3Font* font);
  RetainPtr<CPDF_TransferFunc> GetTransferFunc(
      RetainPtr<const CPDF_Object> obj);


 protected:
  // protected for use by test subclasses.
  RetainPtr<CPDF_TransferFunc> CreateTransferFunc(
      RetainPtr<const CPDF_Object> pObj) const;

 private:
  // TODO(tsepez): investigate this map outliving its font keys.
  std::map<CPDF_Font*, ObservedPtr<CPDF_Type3Cache>> type3_face_map_;
  std::map<RetainPtr<const CPDF_Object>,
           ObservedPtr<CPDF_TransferFunc>,
           std::less<>>
      transfer_func_map_;

};

#endif  // CORE_FPDFAPI_RENDER_CPDF_DOCRENDERDATA_H_
