// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/screen_ai/proto/visual_annotator_proto_convertor.h"

#include "services/screen_ai/public/mojom/screen_ai_service.mojom.h"
#include "ui/gfx/geometry/rect.h"


namespace {


gfx::Rect ProtoToMojo(const chrome_screen_ai::Rect& source) {
  gfx::Rect dest;
  dest.set_x(source.x());
  dest.set_y(source.y());
  dest.set_width(source.width());
  dest.set_height(source.height());
  return dest;
}

screen_ai::mojom::Direction ProtoToMojo(chrome_screen_ai::Direction direction) {
  switch (direction) {
    case chrome_screen_ai::Direction::DIRECTION_UNSPECIFIED:
      return screen_ai::mojom::Direction::DIRECTION_UNSPECIFIED;

    case chrome_screen_ai::Direction::DIRECTION_LEFT_TO_RIGHT:
      return screen_ai::mojom::Direction::DIRECTION_LEFT_TO_RIGHT;

    case chrome_screen_ai::Direction::DIRECTION_RIGHT_TO_LEFT:
      return screen_ai::mojom::Direction::DIRECTION_RIGHT_TO_LEFT;

    case chrome_screen_ai::Direction::DIRECTION_TOP_TO_BOTTOM:
      return screen_ai::mojom::Direction::DIRECTION_TOP_TO_BOTTOM;

    case chrome_screen_ai::Direction_INT_MIN_SENTINEL_DO_NOT_USE_:
    case chrome_screen_ai::Direction_INT_MAX_SENTINEL_DO_NOT_USE_:
      NOTREACHED();
  }
}

}  // namespace

namespace screen_ai {


mojom::VisualAnnotationPtr ConvertProtoToVisualAnnotation(
    const chrome_screen_ai::VisualAnnotation& annotation_proto) {
  auto annotation = screen_ai::mojom::VisualAnnotation::New();

  for (const auto& line : annotation_proto.lines()) {
    auto line_box = screen_ai::mojom::LineBox::New();
    line_box->text_line = line.utf8_string();
    line_box->block_id = line.block_id();
    line_box->language = line.language();
    line_box->paragraph_id = line.paragraph_id();
    line_box->bounding_box = ProtoToMojo(line.bounding_box());
    line_box->bounding_box_angle = line.bounding_box().angle();
    line_box->confidence = line.confidence();

    for (const auto& word : line.words()) {
      auto word_box = screen_ai::mojom::WordBox::New();
      word_box->word = word.utf8_string();
      word_box->language = word.language();
      word_box->bounding_box = ProtoToMojo(word.bounding_box());
      word_box->bounding_box_angle = word.bounding_box().angle();
      word_box->direction = ProtoToMojo(word.direction());
      word_box->whitespace_bounding_box =
          ProtoToMojo(word.whitespace_bounding_box());
      word_box->whitespace_bounding_box_angle =
          word.whitespace_bounding_box().angle();
      word_box->confidence = word.confidence();
      line_box->words.push_back(std::move(word_box));
    }
    annotation->lines.push_back(std::move(line_box));
  }

  return annotation;
}

}  // namespace screen_ai
