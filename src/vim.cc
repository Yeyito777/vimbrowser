#include "vim.h"

#include <algorithm>
#include <cctype>

namespace vimbrowser::vim {

namespace {

bool IsSpace(char c) {
  return std::isspace(static_cast<unsigned char>(c));
}

bool IsWordChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool IsPunct(char c) {
  return !IsSpace(c) && !IsWordChar(c);
}

size_t NormalMax(const LineEditState& state, const std::string& text) {
  if (text.size() <= state.floor) {
    return state.floor;
  }
  return text.size() - 1;
}

void ClampNormalCursor(LineEditState& state, const std::string& text) {
  if (state.floor > text.size()) {
    state.floor = text.size();
  }
  state.cursor = std::clamp(state.cursor, state.floor, NormalMax(state, text));
}

}  // namespace

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

bool MoveWordForward(LineEditState& state, const std::string& text) {
  ClampNormalCursor(state, text);
  const size_t len = text.size();
  if (state.cursor >= NormalMax(state, text)) return false;
  size_t i = state.cursor;
  if (i < len && IsWordChar(text[i])) {
    while (i < len && IsWordChar(text[i])) ++i;
  } else if (i < len && IsPunct(text[i])) {
    while (i < len && IsPunct(text[i])) ++i;
  } else {
    ++i;
  }
  while (i < len && IsSpace(text[i])) ++i;
  state.cursor = std::clamp(i, state.floor, NormalMax(state, text));
  return true;
}

bool MoveWordBackward(LineEditState& state, const std::string& text) {
  ClampNormalCursor(state, text);
  if (state.cursor <= state.floor) return false;
  size_t i = state.cursor - 1;
  while (i > state.floor && IsSpace(text[i])) --i;
  if (IsWordChar(text[i])) {
    while (i > state.floor && IsWordChar(text[i - 1])) --i;
  } else if (IsPunct(text[i])) {
    while (i > state.floor && IsPunct(text[i - 1])) --i;
  }
  state.cursor = std::max(i, state.floor);
  return true;
}

bool MoveWordEnd(LineEditState& state, const std::string& text) {
  ClampNormalCursor(state, text);
  const size_t max = NormalMax(state, text);
  if (state.cursor >= max) return false;
  size_t i = state.cursor + 1;
  while (i < text.size() && IsSpace(text[i])) ++i;
  if (i < text.size() && IsWordChar(text[i])) {
    while (i < max && IsWordChar(text[i + 1])) ++i;
  } else if (i < text.size() && IsPunct(text[i])) {
    while (i < max && IsPunct(text[i + 1])) ++i;
  }
  state.cursor = std::clamp(i, state.floor, max);
  return true;
}

bool MoveWordForwardBig(LineEditState& state, const std::string& text) {
  ClampNormalCursor(state, text);
  const size_t len = text.size();
  if (state.cursor >= NormalMax(state, text)) return false;
  size_t i = state.cursor;
  while (i < len && !IsSpace(text[i])) ++i;
  while (i < len && IsSpace(text[i])) ++i;
  state.cursor = std::clamp(i, state.floor, NormalMax(state, text));
  return true;
}

bool MoveWordBackwardBig(LineEditState& state, const std::string& text) {
  ClampNormalCursor(state, text);
  if (state.cursor <= state.floor) return false;
  size_t i = state.cursor - 1;
  while (i > state.floor && IsSpace(text[i])) --i;
  while (i > state.floor && !IsSpace(text[i - 1])) --i;
  state.cursor = std::max(i, state.floor);
  return true;
}

bool MoveWordEndBig(LineEditState& state, const std::string& text) {
  ClampNormalCursor(state, text);
  const size_t max = NormalMax(state, text);
  if (state.cursor >= max) return false;
  size_t i = state.cursor + 1;
  while (i < text.size() && IsSpace(text[i])) ++i;
  while (i < max && !IsSpace(text[i + 1])) ++i;
  state.cursor = std::clamp(i, state.floor, max);
  return true;
}

bool MoveLineStart(LineEditState& state, const std::string& text) {
  Clamp(state, text);
  state.cursor = state.floor;
  return true;
}

bool MoveLineEnd(LineEditState& state, const std::string& text) {
  Clamp(state, text);
  state.cursor = NormalMax(state, text);
  return true;
}

bool FindForward(LineEditState& state, const std::string& text, char c) {
  ClampNormalCursor(state, text);
  for (size_t i = state.cursor + 1; i < text.size(); ++i) {
    if (text[i] == c) {
      state.cursor = i;
      return true;
    }
  }
  return false;
}

bool FindBackward(LineEditState& state, const std::string& text, char c) {
  ClampNormalCursor(state, text);
  if (state.cursor <= state.floor) return false;
  for (size_t i = state.cursor; i-- > state.floor;) {
    if (text[i] == c) {
      state.cursor = i;
      return true;
    }
  }
  return false;
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

void EnterInsertAtLineStart(LineEditState& state, const std::string& text) {
  Clamp(state, text);
  state.cursor = state.floor;
  state.mode = Mode::kInsert;
}

void EnterInsertAtLineEnd(LineEditState& state, const std::string& text) {
  Clamp(state, text);
  state.cursor = text.size();
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

size_t CursorDisplayOffset(const LineEditState& state, const std::string& text) {
  LineEditState clamped = state;
  Clamp(clamped, text);
  return clamped.cursor;
}

}  // namespace vimbrowser::vim
