#include "vim.h"

#include <algorithm>

namespace vimbrowser::vim {

void Reset(LineEditState& state, size_t cursor, size_t floor) {
  state = LineEditState{};
  state.floor = floor;
  state.cursor = std::max(cursor, floor);
}

void Clamp(LineEditState& state, const std::string& text) {
  state.floor = std::min(state.floor, text.size());
  state.cursor = std::clamp(state.cursor, state.floor, text.size());
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

LineEditResult HandleLineEditKey(LineEditState& state, std::string& text,
                                 KeyInput key) {
  LineEditResult result;
  switch (key.type) {
    case KeyType::kEscape:
      result.cancel = true;
      return result;
    case KeyType::kEnter:
      result.submit = true;
      return result;
    case KeyType::kBackspace:
      result.text_changed = Backspace(state, text);
      return result;
    case KeyType::kChar:
      if (key.ch) {
        result.text_changed = InsertChar(state, text, key.ch);
      }
      return result;
  }

  result.handled = false;
  return result;
}

}  // namespace vimbrowser::vim
