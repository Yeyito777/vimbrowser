// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_SCREEN_AI_PROTO_VISUAL_ANNOTATOR_PROTO_CONVERTOR_H_
#define SERVICES_SCREEN_AI_PROTO_VISUAL_ANNOTATOR_PROTO_CONVERTOR_H_

#include "services/screen_ai/proto/chrome_screen_ai.pb.h"
#include "services/screen_ai/public/mojom/screen_ai_service.mojom.h"


namespace screen_ai {


// Converts a serialized VisualAnnotation proto into a mojo struct.
mojom::VisualAnnotationPtr ConvertProtoToVisualAnnotation(
    const chrome_screen_ai::VisualAnnotation& annotation_proto);

}  // namespace screen_ai

#endif  // SERVICES_SCREEN_AI_PROTO_VISUAL_ANNOTATOR_PROTO_CONVERTOR_H_
