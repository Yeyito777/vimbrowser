// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/yeyito_hints/copy_hints.h"

#include "third_party/blink/renderer/core/clipboard/system_clipboard.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/flat_tree_traversal.h"
#include "third_party/blink/renderer/core/dom/text.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/visual_viewport.h"
#include "third_party/blink/renderer/core/html/forms/text_control_element.h"
#include "third_party/blink/renderer/core/layout/layout_object.h"
#include "third_party/blink/renderer/core/layout/layout_text.h"
#include "third_party/blink/renderer/core/page/page.h"
#include "third_party/blink/renderer/core/style/computed_style.h"

namespace blink::copy_hints {

namespace {

constexpr float kMinimumHintSize = 4.0f;

bool IsVisibleRect(const gfx::RectF& rect, const gfx::Size& viewport_size) {
  if (rect.IsEmpty() || rect.width() < kMinimumHintSize ||
      rect.height() < kMinimumHintSize) {
    return false;
  }
  return rect.Intersects(
      gfx::RectF(0, 0, viewport_size.width(), viewport_size.height()));
}

bool HasVisibleStyle(Element& element) {
  const ComputedStyle* element_style = element.GetComputedStyle();
  if (!element_style || element_style->Visibility() != EVisibility::kVisible) {
    return false;
  }

  for (Node& ancestor : FlatTreeTraversal::InclusiveAncestorsOf(element)) {
    auto* ancestor_element = DynamicTo<Element>(ancestor);
    if (!ancestor_element) {
      continue;
    }
    const ComputedStyle* style = ancestor_element->GetComputedStyle();
    if (!style || style->Opacity() == 0.0f) {
      return false;
    }
  }
  return true;
}

bool AlreadyCollected(Element& element,
                      const HeapVector<HintCandidate>& candidates) {
  for (const HintCandidate& candidate : candidates) {
    if (candidate.element == &element) {
      return true;
    }
  }
  return false;
}

void MaybeAppendCandidate(Element& element,
                          const String& text,
                          const gfx::Size& viewport_size,
                          HeapVector<HintCandidate>& candidates) {
  if (AlreadyCollected(element, candidates) || !HasVisibleStyle(element)) {
    return;
  }

  const String copy_text = text.StripWhiteSpace();
  if (copy_text.empty()) {
    return;
  }

  const gfx::RectF rect(element.VisibleBoundsInLocalRoot());
  if (!IsVisibleRect(rect, viewport_size)) {
    return;
  }

  HintCandidate candidate;
  candidate.element = &element;
  candidate.viewport_rect = rect;
  candidate.copy_text = copy_text;
  candidates.push_back(candidate);
}

Element* CopyContainerForText(Text& text) {
  Element* fallback = nullptr;
  for (Element* element = FlatTreeTraversal::ParentElement(text); element;
       element = FlatTreeTraversal::ParentElement(*element)) {
    LayoutObject* layout_object = element->GetLayoutObject();
    if (!layout_object) {
      continue;
    }
    fallback = element;
    // Group normal inline markup into the nearest rendered text block, while
    // keeping atomic inline controls (buttons, inline-block cards, etc.) as
    // independent copy targets.
    if (!layout_object->IsInline() || layout_object->IsAtomicInline()) {
      return element;
    }
  }
  return fallback;
}

}  // namespace

void CollectCandidates(LocalFrame& frame,
                       HeapVector<HintCandidate>& candidates) {
  candidates.clear();
  Document* document = frame.GetDocument();
  if (!document || !frame.View() || !frame.GetPage()) {
    return;
  }

  document->UpdateStyleAndLayout(DocumentUpdateReason::kInput);
  Element* root = document->documentElement();
  if (!root) {
    return;
  }

  const gfx::Size viewport_size = frame.GetPage()->GetVisualViewport().Size();
  for (Node& node : FlatTreeTraversal::InclusiveDescendantsOf(*root)) {
    if (auto* text_control = DynamicTo<TextControlElement>(node)) {
      MaybeAppendCandidate(*text_control, text_control->Value(), viewport_size,
                           candidates);
      continue;
    }

    auto* text = DynamicTo<Text>(node);
    if (!text || !text->GetLayoutObject() ||
        text->data().StripWhiteSpace().empty()) {
      continue;
    }
    Element* container = CopyContainerForText(*text);
    if (!container) {
      continue;
    }
    MaybeAppendCandidate(*container, container->GetInnerTextWithoutUpdate(),
                         viewport_size, candidates);
  }
}

void ActivateCandidate(LocalFrame&, const HintCandidate& candidate) {
  Element* element = candidate.element.Get();
  if (!element || candidate.copy_text.empty()) {
    return;
  }

  LocalFrame* frame = element->GetDocument().GetFrame();
  if (!frame) {
    return;
  }
  SystemClipboard* clipboard = frame->GetSystemClipboard();
  clipboard->WritePlainText(candidate.copy_text);
  clipboard->CommitWrite();
}

}  // namespace blink::copy_hints
