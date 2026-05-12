#include "vim.h"

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace vimbrowser::vim {

namespace {

struct Range {
  size_t start = 0;
  size_t end = 0;
};

enum class CommandType {
  kNone,
  kMotion,
  kOperator,
  kModeChange,
  kStandalone,
};

enum class Motion {
  kCharLeft,
  kCharRight,
  kWordForward,
  kWordBackward,
  kWordEnd,
  kWordForwardBig,
  kWordBackwardBig,
  kWordEndBig,
  kLineStart,
  kLineEnd,
  kBufferStart,
  kBufferEnd,
};

enum class ModeChangeCursor {
  kBefore,
  kAfter,
  kBol,
  kEol,
};

enum class Standalone {
  kDeleteChar,
  kDeleteCharBefore,
  kDeleteLine,
  kChangeLine,
  kYankLine,
  kDeleteToEnd,
  kChangeToEnd,
  kPasteAfter,
  kPasteBefore,
  kSwapCase,
};

struct Command {
  CommandType type = CommandType::kNone;
  Motion motion = Motion::kCharLeft;
  char op = 0;
  ModeChangeCursor mode_cursor = ModeChangeCursor::kBefore;
  Standalone standalone = Standalone::kDeleteChar;
};

bool IsSpace(char c) {
  return std::isspace(static_cast<unsigned char>(c));
}

bool IsWordChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool IsWORDChar(char c) {
  return !IsSpace(c);
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

size_t ClampNormalPos(const LineEditState& state, const std::string& text,
                      size_t pos) {
  const size_t floor = std::min(state.floor, text.size());
  const size_t max = text.size() <= floor ? floor : text.size() - 1;
  return std::clamp(pos, floor, max);
}

void MarkCursor(LineEditResult& result, size_t before, size_t after) {
  if (before != after) {
    result.cursor_changed = true;
  }
}

void MarkMode(LineEditResult& result, Mode before, Mode after) {
  if (before != after) {
    result.mode_changed = true;
  }
}

bool IsTextObjectKey(char key) {
  switch (key) {
    case 'w':
    case 'W':
    case '"':
    case '\'':
    case '`':
    case '(':
    case ')':
    case 'b':
    case '{':
    case '}':
    case 'B':
    case '[':
    case ']':
    case '<':
    case '>':
      return true;
    default:
      return false;
  }
}

Command LookupCommand(const std::string& key) {
  if (key == "h") return {CommandType::kMotion, Motion::kCharLeft};
  if (key == "l") return {CommandType::kMotion, Motion::kCharRight};
  if (key == "w") return {CommandType::kMotion, Motion::kWordForward};
  if (key == "b") return {CommandType::kMotion, Motion::kWordBackward};
  if (key == "e") return {CommandType::kMotion, Motion::kWordEnd};
  if (key == "W") return {CommandType::kMotion, Motion::kWordForwardBig};
  if (key == "B") return {CommandType::kMotion, Motion::kWordBackwardBig};
  if (key == "E") return {CommandType::kMotion, Motion::kWordEndBig};
  if (key == "0") return {CommandType::kMotion, Motion::kLineStart};
  if (key == "$") return {CommandType::kMotion, Motion::kLineEnd};
  if (key == "gg") return {CommandType::kMotion, Motion::kBufferStart};
  if (key == "G") return {CommandType::kMotion, Motion::kBufferEnd};

  if (key == "i") return {CommandType::kModeChange, Motion::kCharLeft, 0, ModeChangeCursor::kBefore};
  if (key == "a") return {CommandType::kModeChange, Motion::kCharLeft, 0, ModeChangeCursor::kAfter};
  if (key == "I") return {CommandType::kModeChange, Motion::kCharLeft, 0, ModeChangeCursor::kBol};
  if (key == "A") return {CommandType::kModeChange, Motion::kCharLeft, 0, ModeChangeCursor::kEol};

  if (key == "d") return {CommandType::kOperator, Motion::kCharLeft, 'd'};
  if (key == "c") return {CommandType::kOperator, Motion::kCharLeft, 'c'};
  if (key == "y") return {CommandType::kOperator, Motion::kCharLeft, 'y'};

  if (key == "x") return {CommandType::kStandalone, Motion::kCharLeft, 0, ModeChangeCursor::kBefore, Standalone::kDeleteChar};
  if (key == "X") return {CommandType::kStandalone, Motion::kCharLeft, 0, ModeChangeCursor::kBefore, Standalone::kDeleteCharBefore};
  if (key == "dd") return {CommandType::kStandalone, Motion::kCharLeft, 0, ModeChangeCursor::kBefore, Standalone::kDeleteLine};
  if (key == "cc") return {CommandType::kStandalone, Motion::kCharLeft, 0, ModeChangeCursor::kBefore, Standalone::kChangeLine};
  if (key == "yy") return {CommandType::kStandalone, Motion::kCharLeft, 0, ModeChangeCursor::kBefore, Standalone::kYankLine};
  if (key == "D") return {CommandType::kStandalone, Motion::kCharLeft, 0, ModeChangeCursor::kBefore, Standalone::kDeleteToEnd};
  if (key == "C") return {CommandType::kStandalone, Motion::kCharLeft, 0, ModeChangeCursor::kBefore, Standalone::kChangeToEnd};
  if (key == "p") return {CommandType::kStandalone, Motion::kCharLeft, 0, ModeChangeCursor::kBefore, Standalone::kPasteAfter};
  if (key == "P") return {CommandType::kStandalone, Motion::kCharLeft, 0, ModeChangeCursor::kBefore, Standalone::kPasteBefore};
  if (key == "~") return {CommandType::kStandalone, Motion::kCharLeft, 0, ModeChangeCursor::kBefore, Standalone::kSwapCase};

  return {};
}

bool IsPrefix(const std::string& key) {
  return key == "g";
}

void NormalizeRange(Range& range) {
  if (range.start > range.end) std::swap(range.start, range.end);
}

Range EditableRange(const LineEditState& state, const std::string& text) {
  return {std::min(state.floor, text.size()), text.size()};
}

bool DeleteRange(LineEditState& state, std::string& text, Range range) {
  const Range editable = EditableRange(state, text);
  NormalizeRange(range);
  range.start = std::clamp(range.start, editable.start, editable.end);
  range.end = std::clamp(range.end, editable.start, editable.end);
  if (range.start >= range.end) return false;
  text.erase(range.start, range.end - range.start);
  state.cursor = std::min(range.start, text.size());
  if (state.mode == Mode::kNormal) {
    ClampNormal(state, text);
  } else {
    ClampInsert(state, text);
  }
  return true;
}

bool DeleteLine(LineEditState& state, std::string& text) {
  return DeleteRange(state, text, EditableRange(state, text));
}

bool DeleteToEnd(LineEditState& state, std::string& text) {
  return DeleteRange(state, text, {state.cursor, text.size()});
}

bool DeleteCharBefore(LineEditState& state, std::string& text) {
  if (state.cursor <= state.floor) return false;
  return DeleteRange(state, text, {state.cursor - 1, state.cursor});
}

bool ReplaceChar(LineEditState& state, std::string& text, char c) {
  ClampNormal(state, text);
  if (state.cursor >= text.size()) return false;
  text[state.cursor] = c;
  return true;
}

char SwapCase(char c) {
  const unsigned char uc = static_cast<unsigned char>(c);
  if (std::islower(uc)) return static_cast<char>(std::toupper(uc));
  if (std::isupper(uc)) return static_cast<char>(std::tolower(uc));
  return c;
}

bool SwapCaseAtCursor(LineEditState& state, std::string& text, int count) {
  ClampNormal(state, text);
  if (state.cursor >= text.size()) return false;
  const size_t end = std::min(text.size(), state.cursor + static_cast<size_t>(std::max(1, count)));
  if (state.cursor >= end) return false;
  for (size_t i = state.cursor; i < end; ++i) {
    text[i] = SwapCase(text[i]);
  }
  state.cursor = ClampNormalPos(state, text, end - 1);
  return true;
}

void PasteRegister(LineEditState& state, std::string& text, bool after) {
  if (state.register_text.empty()) return;
  const size_t insert_at = after ? std::min(text.size(), state.cursor + 1) : state.cursor;
  text.insert(insert_at, state.register_text);
  state.cursor = ClampNormalPos(state, text, insert_at + state.register_text.size() - 1);
}

bool InnerWord(const LineEditState& state, const std::string& text, Range* out) {
  const Range editable = EditableRange(state, text);
  if (editable.start >= editable.end) return false;
  const size_t pos = ClampNormalPos(state, text, state.cursor);
  const char ch = text[pos];
  size_t start = pos;
  size_t end = pos;
  if (IsWordChar(ch)) {
    while (start > editable.start && IsWordChar(text[start - 1])) --start;
    while (end + 1 < editable.end && IsWordChar(text[end + 1])) ++end;
  } else if (IsSpace(ch)) {
    while (start > editable.start && IsSpace(text[start - 1])) --start;
    while (end + 1 < editable.end && IsSpace(text[end + 1])) ++end;
  } else {
    while (start > editable.start && IsPunct(text[start - 1])) --start;
    while (end + 1 < editable.end && IsPunct(text[end + 1])) ++end;
  }
  *out = {start, end + 1};
  return true;
}

bool AWord(const LineEditState& state, const std::string& text, Range* out) {
  if (!InnerWord(state, text, out)) return false;
  const Range editable = EditableRange(state, text);
  if (out->end < editable.end && IsSpace(text[out->end])) {
    while (out->end < editable.end && IsSpace(text[out->end])) ++out->end;
    return true;
  }
  if (out->start > editable.start && IsSpace(text[out->start - 1])) {
    while (out->start > editable.start && IsSpace(text[out->start - 1])) --out->start;
  }
  return true;
}

bool InnerWORD(const LineEditState& state, const std::string& text, Range* out) {
  const Range editable = EditableRange(state, text);
  if (editable.start >= editable.end) return false;
  const size_t pos = ClampNormalPos(state, text, state.cursor);
  const char ch = text[pos];
  size_t start = pos;
  size_t end = pos;
  if (!IsWORDChar(ch)) {
    while (start > editable.start && !IsWORDChar(text[start - 1])) --start;
    while (end + 1 < editable.end && !IsWORDChar(text[end + 1])) ++end;
  } else {
    while (start > editable.start && IsWORDChar(text[start - 1])) --start;
    while (end + 1 < editable.end && IsWORDChar(text[end + 1])) ++end;
  }
  *out = {start, end + 1};
  return true;
}

bool AWORD(const LineEditState& state, const std::string& text, Range* out) {
  if (!InnerWORD(state, text, out)) return false;
  const Range editable = EditableRange(state, text);
  if (out->end < editable.end && IsSpace(text[out->end])) {
    while (out->end < editable.end && IsSpace(text[out->end])) ++out->end;
    return true;
  }
  if (out->start > editable.start && IsSpace(text[out->start - 1])) {
    while (out->start > editable.start && IsSpace(text[out->start - 1])) --out->start;
  }
  return true;
}

bool QuotePair(const LineEditState& state, const std::string& text, char quote,
               Range* out) {
  const Range editable = EditableRange(state, text);
  std::vector<size_t> positions;
  for (size_t i = editable.start; i < editable.end; ++i) {
    if (text[i] == quote && (i == editable.start || text[i - 1] != '\\')) {
      positions.push_back(i);
    }
  }
  if (positions.size() < 2) return false;
  for (size_t i = 0; i + 1 < positions.size(); i += 2) {
    if (state.cursor >= positions[i] && state.cursor <= positions[i + 1]) {
      *out = {positions[i], positions[i + 1] + 1};
      return true;
    }
  }
  for (size_t i = 0; i + 1 < positions.size(); i += 2) {
    if (positions[i] > state.cursor) {
      *out = {positions[i], positions[i + 1] + 1};
      return true;
    }
  }
  return false;
}

size_t FindMatchingClose(const std::string& text, size_t open_pos, char open,
                         char close) {
  int depth = 0;
  for (size_t i = open_pos + 1; i < text.size(); ++i) {
    if (text[i] == open) ++depth;
    if (text[i] == close) {
      if (depth == 0) return i;
      --depth;
    }
  }
  return std::string::npos;
}

size_t FindMatchingOpen(const std::string& text, size_t close_pos, char open,
                        char close) {
  int depth = 0;
  for (size_t i = close_pos; i-- > 0;) {
    if (text[i] == close) ++depth;
    if (text[i] == open) {
      if (depth == 0) return i;
      --depth;
    }
  }
  return std::string::npos;
}

bool PairRange(const LineEditState& state, const std::string& text, char open,
               char close, Range* out) {
  const Range editable = EditableRange(state, text);
  if (editable.start >= editable.end) return false;
  const size_t cursor = ClampNormalPos(state, text, state.cursor);
  if (text[cursor] == open) {
    const size_t close_pos = FindMatchingClose(text, cursor, open, close);
    if (close_pos != std::string::npos) {
      *out = {cursor, close_pos + 1};
      return true;
    }
  }
  if (text[cursor] == close) {
    const size_t open_pos = FindMatchingOpen(text, cursor, open, close);
    if (open_pos != std::string::npos && open_pos >= editable.start) {
      *out = {open_pos, cursor + 1};
      return true;
    }
  }
  int depth = 0;
  for (size_t i = cursor; i-- > editable.start;) {
    if (text[i] == close) ++depth;
    if (text[i] == open) {
      if (depth == 0) {
        const size_t close_pos = FindMatchingClose(text, i, open, close);
        if (close_pos != std::string::npos && close_pos >= cursor && close_pos < editable.end) {
          *out = {i, close_pos + 1};
          return true;
        }
      } else {
        --depth;
      }
    }
  }
  for (size_t i = cursor + 1; i < editable.end; ++i) {
    if (text[i] == open) {
      const size_t close_pos = FindMatchingClose(text, i, open, close);
      if (close_pos != std::string::npos && close_pos < editable.end) {
        *out = {i, close_pos + 1};
        return true;
      }
    }
  }
  return false;
}

bool ResolveTextObject(const LineEditState& state, const std::string& text,
                       char modifier, char key, Range* out) {
  bool ok = false;
  switch (key) {
    case 'w': ok = modifier == 'i' ? InnerWord(state, text, out) : AWord(state, text, out); break;
    case 'W': ok = modifier == 'i' ? InnerWORD(state, text, out) : AWORD(state, text, out); break;
    case '"':
    case '\'':
    case '`':
      ok = QuotePair(state, text, key, out);
      if (ok && modifier == 'i' && out->end > out->start + 1) {
        ++out->start;
        --out->end;
      }
      break;
    case '(':
    case ')':
    case 'b': ok = PairRange(state, text, '(', ')', out); break;
    case '{':
    case '}':
    case 'B': ok = PairRange(state, text, '{', '}', out); break;
    case '[':
    case ']': ok = PairRange(state, text, '[', ']', out); break;
    case '<':
    case '>': ok = PairRange(state, text, '<', '>', out); break;
    default: ok = false; break;
  }
  if (ok && modifier == 'i' && (key == '(' || key == ')' || key == 'b' || key == '{' ||
                                key == '}' || key == 'B' || key == '[' || key == ']' ||
                                key == '<' || key == '>') && out->end > out->start + 1) {
    ++out->start;
    --out->end;
  }
  return ok;
}

bool ApplyOperatorToRange(LineEditState& state, std::string& text, char op,
                          Range range, LineEditResult& result) {
  NormalizeRange(range);
  if (range.start == range.end) return false;
  const size_t old_cursor = state.cursor;
  const Mode old_mode = state.mode;
  switch (op) {
    case 'd':
      state.register_text = text.substr(range.start, range.end - range.start);
      if (DeleteRange(state, text, range)) result.text_changed = true;
      break;
    case 'c':
      state.register_text = text.substr(range.start, range.end - range.start);
      state.mode = Mode::kInsert;
      if (DeleteRange(state, text, range)) result.text_changed = true;
      ClampInsert(state, text);
      break;
    case 'y':
      state.register_text = text.substr(range.start, range.end - range.start);
      break;
    default:
      return false;
  }
  MarkCursor(result, old_cursor, state.cursor);
  MarkMode(result, old_mode, state.mode);
  return true;
}

bool ExecuteMotion(LineEditState& state, const std::string& text, Motion motion) {
  switch (motion) {
    case Motion::kCharLeft: return MoveLeft(state, text);
    case Motion::kCharRight: return MoveRight(state, text);
    case Motion::kWordForward: return MoveWordForward(state, text);
    case Motion::kWordBackward: return MoveWordBackward(state, text);
    case Motion::kWordEnd: return MoveWordEnd(state, text);
    case Motion::kWordForwardBig: return MoveWordForwardBig(state, text);
    case Motion::kWordBackwardBig: return MoveWordBackwardBig(state, text);
    case Motion::kWordEndBig: return MoveWordEndBig(state, text);
    case Motion::kLineStart: return MoveLineStart(state, text);
    case Motion::kLineEnd: return MoveLineEnd(state, text);
    case Motion::kBufferStart: return MoveLineStart(state, text);
    case Motion::kBufferEnd: return MoveLineEnd(state, text);
  }
  return false;
}

size_t MotionTarget(const LineEditState& state, const std::string& text,
                    Motion motion) {
  const Range editable = EditableRange(state, text);
  size_t pos = std::clamp(state.cursor, editable.start, editable.end);
  const size_t len = editable.end;
  switch (motion) {
    case Motion::kCharLeft:
      return pos <= editable.start ? editable.start : pos - 1;
    case Motion::kCharRight:
      return std::min(pos + 1, len);
    case Motion::kLineStart:
    case Motion::kBufferStart:
      return editable.start;
    case Motion::kLineEnd:
    case Motion::kBufferEnd:
      return len;
    case Motion::kWordForward: {
      if (pos >= len) return pos;
      size_t i = pos;
      if (IsWordChar(text[i])) {
        while (i < len && IsWordChar(text[i])) ++i;
      } else if (IsPunct(text[i])) {
        while (i < len && IsPunct(text[i])) ++i;
      } else {
        ++i;
      }
      while (i < len && IsSpace(text[i])) ++i;
      return i;
    }
    case Motion::kWordBackward: {
      if (pos <= editable.start) return editable.start;
      size_t i = pos - 1;
      while (i > editable.start && IsSpace(text[i])) --i;
      if (IsWordChar(text[i])) {
        while (i > editable.start && IsWordChar(text[i - 1])) --i;
      } else if (IsPunct(text[i])) {
        while (i > editable.start && IsPunct(text[i - 1])) --i;
      }
      return i;
    }
    case Motion::kWordEnd: {
      if (pos >= len - 1) return len == 0 ? editable.start : len - 1;
      size_t i = pos + 1;
      while (i < len && IsSpace(text[i])) ++i;
      if (i < len && IsWordChar(text[i])) {
        while (i < len - 1 && IsWordChar(text[i + 1])) ++i;
      } else if (i < len && IsPunct(text[i])) {
        while (i < len - 1 && IsPunct(text[i + 1])) ++i;
      }
      return i;
    }
    case Motion::kWordForwardBig: {
      size_t i = pos;
      while (i < len && !IsSpace(text[i])) ++i;
      while (i < len && IsSpace(text[i])) ++i;
      return i;
    }
    case Motion::kWordBackwardBig: {
      if (pos <= editable.start) return editable.start;
      size_t i = pos - 1;
      while (i > editable.start && IsSpace(text[i])) --i;
      while (i > editable.start && !IsSpace(text[i - 1])) --i;
      return i;
    }
    case Motion::kWordEndBig: {
      if (pos >= len - 1) return len == 0 ? editable.start : len - 1;
      size_t i = pos + 1;
      while (i < len && IsSpace(text[i])) ++i;
      while (i < len - 1 && !IsSpace(text[i + 1])) ++i;
      return i;
    }
  }
  return pos;
}

bool ExecuteOperatorMotion(LineEditState& state, std::string& text, char op,
                           Motion motion, int count, LineEditResult& result) {
  size_t target = state.cursor;
  LineEditState target_state = state;
  for (int i = 0; i < std::max(1, count); ++i) {
    target = MotionTarget(target_state, text, motion);
    target_state.cursor = target;
  }
  const size_t start = std::min(state.cursor, target);
  const size_t end = std::max(state.cursor, target);
  if (start == end) return false;
  return ApplyOperatorToRange(state, text, op, {start, end}, result);
}

LineEditResult ExecuteCommand(Command cmd, LineEditState& state, std::string& text) {
  LineEditResult result;
  const int count = state.count ? state.count : 1;
  const size_t old_cursor = state.cursor;
  const Mode old_mode = state.mode;

  switch (cmd.type) {
    case CommandType::kMotion:
      for (int i = 0; i < count; ++i) ExecuteMotion(state, text, cmd.motion);
      ClampNormal(state, text);
      ResetPending(state);
      MarkCursor(result, old_cursor, state.cursor);
      return result;
    case CommandType::kOperator:
      state.pending_operator = cmd.op;
      state.pending_operator_key = cmd.op;
      state.pending_keys.clear();
      state.count = 0;
      result.pending = true;
      return result;
    case CommandType::kModeChange:
      switch (cmd.mode_cursor) {
        case ModeChangeCursor::kBefore: EnterInsert(state, text); break;
        case ModeChangeCursor::kAfter: EnterInsertAfter(state, text); break;
        case ModeChangeCursor::kBol: EnterInsertAtLineStart(state, text); break;
        case ModeChangeCursor::kEol: EnterInsertAtLineEnd(state, text); break;
      }
      ResetPending(state);
      MarkCursor(result, old_cursor, state.cursor);
      MarkMode(result, old_mode, state.mode);
      return result;
    case CommandType::kStandalone:
      ResetPending(state);
      switch (cmd.standalone) {
        case Standalone::kDeleteChar:
          state.register_text = state.cursor < text.size() ? text.substr(state.cursor, 1) : "";
          if (DeleteAtCursor(state, text)) result.text_changed = true;
          break;
        case Standalone::kDeleteCharBefore:
          state.register_text = state.cursor > state.floor ? text.substr(state.cursor - 1, 1) : "";
          if (DeleteCharBefore(state, text)) result.text_changed = true;
          break;
        case Standalone::kDeleteLine:
          state.register_text = text.substr(state.floor);
          if (DeleteLine(state, text)) result.text_changed = true;
          break;
        case Standalone::kChangeLine:
          state.register_text = text.substr(state.floor);
          state.mode = Mode::kInsert;
          if (DeleteLine(state, text)) result.text_changed = true;
          ClampInsert(state, text);
          break;
        case Standalone::kYankLine:
          state.register_text = text.substr(state.floor);
          break;
        case Standalone::kDeleteToEnd:
          state.register_text = state.cursor < text.size() ? text.substr(state.cursor) : "";
          if (DeleteToEnd(state, text)) result.text_changed = true;
          break;
        case Standalone::kChangeToEnd:
          state.register_text = state.cursor < text.size() ? text.substr(state.cursor) : "";
          state.mode = Mode::kInsert;
          if (DeleteToEnd(state, text)) result.text_changed = true;
          ClampInsert(state, text);
          break;
        case Standalone::kPasteAfter:
          PasteRegister(state, text, true);
          result.text_changed = true;
          break;
        case Standalone::kPasteBefore:
          PasteRegister(state, text, false);
          result.text_changed = true;
          break;
        case Standalone::kSwapCase:
          if (SwapCaseAtCursor(state, text, count)) result.text_changed = true;
          break;
      }
      MarkCursor(result, old_cursor, state.cursor);
      MarkMode(result, old_mode, state.mode);
      return result;
    case CommandType::kNone:
      return result;
  }
  return result;
}

LineEditResult HandleNormalKey(LineEditState& state, std::string& text,
                               KeyInput key) {
  LineEditResult result;
  if (key.type == KeyType::kEscape || key.type == KeyType::kEnter ||
      key.type == KeyType::kBackspace) {
    result.handled = false;
    return result;
  }
  if (key.type != KeyType::kChar || key.ch == 0) return result;

  const char ch = key.ch;

  if (state.pending_find) {
    const size_t old_cursor = state.cursor;
    state.last_find = ch;
    state.last_find_direction = state.pending_find;
    if (state.pending_operator) {
      LineEditState target = state;
      if (state.pending_find == 'f') FindForward(target, text, ch);
      else FindBackward(target, text, ch);
      if (target.cursor != state.cursor) {
        size_t start = std::min(state.cursor, target.cursor);
        size_t end = std::max(state.cursor, target.cursor) + 1;
        ApplyOperatorToRange(state, text, state.pending_operator, {start, end}, result);
      }
    } else {
      if (state.pending_find == 'f') FindForward(state, text, ch);
      else FindBackward(state, text, ch);
      MarkCursor(result, old_cursor, state.cursor);
    }
    ResetPending(state);
    return result;
  }

  if (state.pending_replace) {
    if (ReplaceChar(state, text, ch)) result.text_changed = true;
    ResetPending(state);
    return result;
  }

  if ((ch >= '1' && ch <= '9') || (ch == '0' && state.count != 0)) {
    state.count = state.count * 10 + (ch - '0');
    result.pending = true;
    return result;
  }

  if (ch == 'r' && !state.pending_operator) {
    state.pending_replace = true;
    result.pending = true;
    return result;
  }

  if (ch == 'f' || ch == 'F') {
    state.pending_find = ch;
    result.pending = true;
    return result;
  }

  if (ch == ';' || ch == ',') {
    if (!state.last_find) return result;
    char dir = state.last_find_direction;
    if (ch == ',') dir = dir == 'f' ? 'F' : 'f';
    if (state.pending_operator) {
      LineEditState target = state;
      if (dir == 'f') FindForward(target, text, state.last_find);
      else FindBackward(target, text, state.last_find);
      if (target.cursor != state.cursor) {
        ApplyOperatorToRange(state, text, state.pending_operator,
                             {std::min(state.cursor, target.cursor),
                              std::max(state.cursor, target.cursor) + 1},
                             result);
      }
      ResetPending(state);
    } else {
      const size_t old_cursor = state.cursor;
      if (dir == 'f') FindForward(state, text, state.last_find);
      else FindBackward(state, text, state.last_find);
      MarkCursor(result, old_cursor, state.cursor);
    }
    return result;
  }

  if (state.pending_operator && state.pending_text_object_modifier) {
    if (IsTextObjectKey(ch)) {
      Range range;
      if (ResolveTextObject(state, text, state.pending_text_object_modifier, ch, &range)) {
        ApplyOperatorToRange(state, text, state.pending_operator, range, result);
      }
    }
    ResetPending(state);
    return result;
  }

  if (state.pending_operator) {
    if (ch == 'i' || ch == 'a') {
      state.pending_text_object_modifier = ch;
      result.pending = true;
      return result;
    }
    if (ch == state.pending_operator_key) {
      const char op = state.pending_operator;
      if (op == 'y') {
        state.register_text = text.substr(state.floor);
      } else if (op == 'c') {
        state.register_text = text.substr(state.floor);
        state.mode = Mode::kInsert;
        if (DeleteLine(state, text)) result.text_changed = true;
        ClampInsert(state, text);
        result.mode_changed = true;
      } else {
        state.register_text = text.substr(state.floor);
        if (DeleteLine(state, text)) result.text_changed = true;
      }
      ResetPending(state);
      return result;
    }
    const Command cmd = LookupCommand(std::string(1, ch));
    if (cmd.type == CommandType::kMotion) {
      ExecuteOperatorMotion(state, text, state.pending_operator, cmd.motion,
                            state.count ? state.count : 1, result);
    }
    ResetPending(state);
    return result;
  }

  const std::string full_key = state.pending_keys + ch;
  Command cmd = LookupCommand(full_key);
  if (cmd.type != CommandType::kNone) {
    state.pending_keys.clear();
    result = ExecuteCommand(cmd, state, text);
    if (cmd.type == CommandType::kOperator) state.pending_operator_key = ch;
    return result;
  }
  if (IsPrefix(full_key)) {
    state.pending_keys = full_key;
    result.pending = true;
    return result;
  }

  ResetPending(state);
  return result;
}

}  // namespace

void Reset(LineEditState& state, size_t cursor, size_t floor, Mode mode) {
  std::string keep_register = state.register_text;
  char last_find = state.last_find;
  char last_find_direction = state.last_find_direction;
  state = LineEditState{};
  state.floor = floor;
  state.cursor = std::max(cursor, floor);
  state.mode = mode;
  state.register_text = std::move(keep_register);
  state.last_find = last_find;
  state.last_find_direction = last_find_direction;
}

void ResetPending(LineEditState& state) {
  state.pending_operator = 0;
  state.pending_operator_key = 0;
  state.pending_text_object_modifier = 0;
  state.pending_keys.clear();
  state.count = 0;
  state.pending_find = 0;
  state.pending_replace = false;
}

void ClampInsert(LineEditState& state, const std::string& text) {
  if (state.floor > text.size()) {
    state.floor = text.size();
  }
  state.cursor = std::clamp(state.cursor, state.floor, text.size());
}

void ClampNormal(LineEditState& state, const std::string& text) {
  if (state.floor > text.size()) {
    state.floor = text.size();
  }
  state.cursor = ClampNormalPos(state, text, state.cursor);
}

void Clamp(LineEditState& state, const std::string& text) {
  if (state.mode == Mode::kInsert) {
    ClampInsert(state, text);
  } else {
    ClampNormal(state, text);
  }
}

bool MoveLeft(LineEditState& state, const std::string& text) {
  ClampNormal(state, text);
  if (state.cursor <= state.floor) {
    return false;
  }
  --state.cursor;
  return true;
}

bool MoveRight(LineEditState& state, const std::string& text) {
  ClampNormal(state, text);
  const size_t max = NormalMax(state, text);
  if (state.cursor >= max) {
    return false;
  }
  ++state.cursor;
  return true;
}

bool MoveWordForward(LineEditState& state, const std::string& text) {
  ClampNormal(state, text);
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
  state.cursor = ClampNormalPos(state, text, i);
  return true;
}

bool MoveWordBackward(LineEditState& state, const std::string& text) {
  ClampNormal(state, text);
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
  ClampNormal(state, text);
  const size_t max = NormalMax(state, text);
  if (state.cursor >= max) return false;
  size_t i = state.cursor + 1;
  while (i < text.size() && IsSpace(text[i])) ++i;
  if (i < text.size() && IsWordChar(text[i])) {
    while (i < max && IsWordChar(text[i + 1])) ++i;
  } else if (i < text.size() && IsPunct(text[i])) {
    while (i < max && IsPunct(text[i + 1])) ++i;
  }
  state.cursor = ClampNormalPos(state, text, i);
  return true;
}

bool MoveWordForwardBig(LineEditState& state, const std::string& text) {
  ClampNormal(state, text);
  const size_t len = text.size();
  if (state.cursor >= NormalMax(state, text)) return false;
  size_t i = state.cursor;
  while (i < len && !IsSpace(text[i])) ++i;
  while (i < len && IsSpace(text[i])) ++i;
  state.cursor = ClampNormalPos(state, text, i);
  return true;
}

bool MoveWordBackwardBig(LineEditState& state, const std::string& text) {
  ClampNormal(state, text);
  if (state.cursor <= state.floor) return false;
  size_t i = state.cursor - 1;
  while (i > state.floor && IsSpace(text[i])) --i;
  while (i > state.floor && !IsSpace(text[i - 1])) --i;
  state.cursor = std::max(i, state.floor);
  return true;
}

bool MoveWordEndBig(LineEditState& state, const std::string& text) {
  ClampNormal(state, text);
  const size_t max = NormalMax(state, text);
  if (state.cursor >= max) return false;
  size_t i = state.cursor + 1;
  while (i < text.size() && IsSpace(text[i])) ++i;
  while (i < max && !IsSpace(text[i + 1])) ++i;
  state.cursor = ClampNormalPos(state, text, i);
  return true;
}

bool MoveLineStart(LineEditState& state, const std::string& text) {
  ClampNormal(state, text);
  state.cursor = state.floor;
  return true;
}

bool MoveLineEnd(LineEditState& state, const std::string& text) {
  ClampNormal(state, text);
  state.cursor = NormalMax(state, text);
  return true;
}

bool FindForward(LineEditState& state, const std::string& text, char c) {
  ClampNormal(state, text);
  for (size_t i = state.cursor + 1; i < text.size(); ++i) {
    if (text[i] == c) {
      state.cursor = i;
      return true;
    }
  }
  return false;
}

bool FindBackward(LineEditState& state, const std::string& text, char c) {
  ClampNormal(state, text);
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
  ClampNormal(state, text);
  state.mode = Mode::kInsert;
}

void EnterInsertAfter(LineEditState& state, const std::string& text) {
  ClampNormal(state, text);
  if (state.cursor < text.size()) {
    ++state.cursor;
  }
  state.mode = Mode::kInsert;
}

void EnterInsertAtLineStart(LineEditState& state, const std::string& text) {
  ClampNormal(state, text);
  state.cursor = state.floor;
  state.mode = Mode::kInsert;
}

void EnterInsertAtLineEnd(LineEditState& state, const std::string& text) {
  ClampInsert(state, text);
  state.cursor = text.size();
  state.mode = Mode::kInsert;
}

void LeaveInsert(LineEditState& state, const std::string& text) {
  ClampInsert(state, text);
  if (state.cursor == text.size() && state.cursor > state.floor) {
    --state.cursor;
  }
  state.mode = Mode::kNormal;
  ClampNormal(state, text);
  ResetPending(state);
}

bool InsertChar(LineEditState& state, std::string& text, char c) {
  ClampInsert(state, text);
  text.insert(text.begin() + static_cast<std::ptrdiff_t>(state.cursor), c);
  ++state.cursor;
  return true;
}

bool Backspace(LineEditState& state, std::string& text) {
  ClampInsert(state, text);
  if (state.cursor <= state.floor) {
    return false;
  }
  text.erase(text.begin() + static_cast<std::ptrdiff_t>(state.cursor - 1));
  --state.cursor;
  return true;
}

bool DeleteAtCursor(LineEditState& state, std::string& text) {
  ClampNormal(state, text);
  if (state.cursor >= text.size()) {
    return false;
  }
  text.erase(text.begin() + static_cast<std::ptrdiff_t>(state.cursor));
  ClampNormal(state, text);
  return true;
}

size_t CursorDisplayOffset(const LineEditState& state, const std::string& text) {
  LineEditState clamped = state;
  Clamp(clamped, text);
  return clamped.cursor;
}

LineEditResult HandleLineEditKey(LineEditState& state, std::string& text,
                                 KeyInput key) {
  LineEditResult result;
  if (state.mode == Mode::kInsert) {
    if (key.type == KeyType::kEscape) {
      if (key.shift) {
        result.cancel = true;
        return result;
      }
      const size_t old_cursor = state.cursor;
      const Mode old_mode = state.mode;
      LeaveInsert(state, text);
      MarkCursor(result, old_cursor, state.cursor);
      MarkMode(result, old_mode, state.mode);
      return result;
    }
    if (key.type == KeyType::kEnter) {
      result.submit = true;
      return result;
    }
    if (key.type == KeyType::kBackspace) {
      if (Backspace(state, text)) result.text_changed = true;
      return result;
    }
    if (key.type == KeyType::kChar && key.ch) {
      InsertChar(state, text, key.ch);
      result.text_changed = true;
      return result;
    }
    return result;
  }

  if (state.mode == Mode::kNormal) {
    if (key.type == KeyType::kEscape) {
      result.cancel = true;
      return result;
    }
    if (key.type == KeyType::kEnter) {
      result.submit = true;
      return result;
    }
    return HandleNormalKey(state, text, key);
  }

  result.handled = false;
  return result;
}

}  // namespace vimbrowser::vim
