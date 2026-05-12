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

struct LineEditState {
  Mode mode = Mode::kInsert;
  size_t cursor = 0;
  size_t floor = 0;
};

void Reset(LineEditState& state, size_t cursor, size_t floor = 0,
           Mode mode = Mode::kInsert);
void Clamp(LineEditState& state, const std::string& text);
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

}  // namespace vimbrowser::vim
