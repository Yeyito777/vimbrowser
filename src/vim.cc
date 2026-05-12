#include "vim.h"

#include <algorithm>

namespace vimbrowser::vim {

namespace {
constexpr const char* kCursorGlyph = "█";
constexpr const char* kBarCursorGlyph = "|";
}

void Reset(LineEditState& state, size_t cursor, size_t floor, Mode mode) {
  state.floor = floor;
  state.cursor = std::max(cursor, floor);
  state.mode = mode;
}

void Clamp(LineEditState& state, const std::string& text) {
  if (state.floor > text.size()) {
    state.floor = text.size();
  }
  state.cursor = std::clamp(state.cursor, state.floor, text.size());
}

bool MoveLeft(LineEditState& state, const std::string& text) {
  Clamp(state, text);
  if (state.cursor <= state.floor) {
    return false;
  }
  --state.cursor;
  return true;
}

bool MoveRight(LineEditState& state, const std::string& text) {
  Clamp(state, text);
  if (state.cursor >= text.size()) {
    return false;
  }
  ++state.cursor;
  return true;
}

void EnterInsert(LineEditState& state, const std::string& text) {
  Clamp(state, text);
  state.mode = Mode::kInsert;
}

void EnterInsertAfter(LineEditState& state, const std::string& text) {
  Clamp(state, text);
  if (state.cursor < text.size()) {
    ++state.cursor;
  }
  state.mode = Mode::kInsert;
}

void LeaveInsert(LineEditState& state, const std::string& text) {
  Clamp(state, text);
  if (state.cursor == text.size() && state.cursor > state.floor) {
    --state.cursor;
  }
  state.mode = Mode::kNormal;
}

bool InsertChar(LineEditState& state, std::string& text, char c) {
  Clamp(state, text);
  text.insert(text.begin() + static_cast<std::ptrdiff_t>(state.cursor), c);
  ++state.cursor;
  return true;
}

bool Backspace(LineEditState& state, std::string& text) {
  Clamp(state, text);
  if (state.cursor <= state.floor) {
    return false;
  }
  text.erase(text.begin() + static_cast<std::ptrdiff_t>(state.cursor - 1));
  --state.cursor;
  return true;
}

bool DeleteAtCursor(LineEditState& state, std::string& text) {
  Clamp(state, text);
  if (state.cursor >= text.size()) {
    return false;
  }
  text.erase(text.begin() + static_cast<std::ptrdiff_t>(state.cursor));
  Clamp(state, text);
  return true;
}

std::string WithCursor(const LineEditState& state, const std::string& text) {
  LineEditState clamped = state;
  Clamp(clamped, text);
  std::string rendered = text;
  if (clamped.mode == Mode::kInsert) {
    rendered.insert(clamped.cursor, kBarCursorGlyph);
  } else if (clamped.cursor < rendered.size()) {
    rendered.replace(clamped.cursor, 1, kCursorGlyph);
  } else {
    rendered.insert(clamped.cursor, kCursorGlyph);
  }
  return rendered;
}

size_t CursorDisplayOffset(const LineEditState& state, const std::string& text) {
  LineEditState clamped = state;
  Clamp(clamped, text);
  return clamped.cursor;
}

}  // namespace vimbrowser::vim
