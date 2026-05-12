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
  bool shift = false;
};

struct LineEditResult {
  bool handled = true;
  bool submit = false;
  bool cancel = false;
  bool text_changed = false;
  bool cursor_changed = false;
  bool mode_changed = false;
  bool pending = false;
};

struct LineEditState {
  Mode mode = Mode::kInsert;
  size_t cursor = 0;
  size_t floor = 0;

  char pending_operator = 0;
  char pending_operator_key = 0;
  char pending_text_object_modifier = 0;
  std::string pending_keys;
  int count = 0;
  char pending_find = 0;
  char last_find = 0;
  char last_find_direction = 0;
  bool pending_replace = false;
  std::string register_text;
};

void Reset(LineEditState& state, size_t cursor, size_t floor = 0,
           Mode mode = Mode::kInsert);
void ResetPending(LineEditState& state);
void Clamp(LineEditState& state, const std::string& text);
void ClampInsert(LineEditState& state, const std::string& text);
void ClampNormal(LineEditState& state, const std::string& text);
bool MoveLeft(LineEditState& state, const std::string& text);
bool MoveRight(LineEditState& state, const std::string& text);
bool MoveWordForward(LineEditState& state, const std::string& text);
bool MoveWordBackward(LineEditState& state, const std::string& text);
bool MoveWordEnd(LineEditState& state, const std::string& text);
bool MoveWordForwardBig(LineEditState& state, const std::string& text);
bool MoveWordBackwardBig(LineEditState& state, const std::string& text);
bool MoveWordEndBig(LineEditState& state, const std::string& text);
bool MoveLineStart(LineEditState& state, const std::string& text);
bool MoveLineEnd(LineEditState& state, const std::string& text);
bool FindForward(LineEditState& state, const std::string& text, char c);
bool FindBackward(LineEditState& state, const std::string& text, char c);
void EnterInsert(LineEditState& state, const std::string& text);
void EnterInsertAfter(LineEditState& state, const std::string& text);
void EnterInsertAtLineStart(LineEditState& state, const std::string& text);
void EnterInsertAtLineEnd(LineEditState& state, const std::string& text);
void LeaveInsert(LineEditState& state, const std::string& text);
bool InsertChar(LineEditState& state, std::string& text, char c);
bool Backspace(LineEditState& state, std::string& text);
bool DeleteAtCursor(LineEditState& state, std::string& text);
size_t CursorDisplayOffset(const LineEditState& state, const std::string& text);

LineEditResult HandleLineEditKey(LineEditState& state, std::string& text,
                                 KeyInput key);

}  // namespace vimbrowser::vim
