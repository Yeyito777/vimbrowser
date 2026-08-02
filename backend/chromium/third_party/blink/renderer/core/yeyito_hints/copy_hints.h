// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_YEYITO_HINTS_COPY_HINTS_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_YEYITO_HINTS_COPY_HINTS_H_

#include "third_party/blink/renderer/core/yeyito_hints/candidate.h"
#include "third_party/blink/renderer/platform/heap/collection_support/heap_vector.h"

namespace blink {

class LocalFrame;

namespace copy_hints {

void CollectCandidates(LocalFrame& frame,
                       HeapVector<HintCandidate>& candidates);
void ActivateCandidate(LocalFrame& controller_frame,
                       const HintCandidate& candidate);

}  // namespace copy_hints

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_YEYITO_HINTS_COPY_HINTS_H_
