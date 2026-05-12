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
void EnterInsert(LineEditState& state, const std::string& text);
void EnterInsertAfter(LineEditState& state, const std::string& text);
void LeaveInsert(LineEditState& state, const std::string& text);
bool InsertChar(LineEditState& state, std::string& text, char c);
bool Backspace(LineEditState& state, std::string& text);
bool DeleteAtCursor(LineEditState& state, std::string& text);
std::string WithCursor(const LineEditState& state, const std::string& text);
size_t CursorDisplayOffset(const LineEditState& state, const std::string& text);

}  // namespace vimbrowser::vim
