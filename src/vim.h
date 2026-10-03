#pragma once

#include <cstddef>
#include <string>

namespace vimbrowser::vim {

enum class Mode {
  kWebsiteNormal,
  kNormal,
  kInsert,
  kVisual,
};

enum class KeyType {
  kChar,
  kEscape,
  kEnter,
  kBackspace,
};

struct KeyInput {
  KeyType type = KeyType::kChar;
  char ch = 0;
};

struct LineEditResult {
  bool handled = true;
  bool submit = false;
  bool cancel = false;
  bool text_changed = false;
  bool cursor_changed = false;
};

// The command line is deliberately insert-only. Website and DevTools Vim modes
// are represented by Mode above; command editing has no normal-mode state.
struct LineEditState {
  size_t cursor = 0;
  size_t floor = 0;
};

void Reset(LineEditState& state, size_t cursor, size_t floor = 0);
void Clamp(LineEditState& state, const std::string& text);
bool InsertChar(LineEditState& state, std::string& text, char c);
bool Backspace(LineEditState& state, std::string& text);

LineEditResult HandleLineEditKey(LineEditState& state, std::string& text,
                                 KeyInput key);

}  // namespace vimbrowser::vim
