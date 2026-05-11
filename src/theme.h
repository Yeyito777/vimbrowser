#pragma once

#include "include/internal/cef_types.h"

namespace vimbrowser::theme {

constexpr cef_color_t Rgb(int r, int g, int b) {
  return CefColorSetARGB(255, r, g, b);
}

constexpr cef_color_t kTransparent = CefColorSetARGB(0, 0, 0, 0);

// Whale theme, mirrored from ~/Workspace/exocortex/tui/src/themes/whale.ts.
constexpr cef_color_t kAppBg = Rgb(0, 5, 15);             // #00050f
constexpr cef_color_t kTopbarBg = Rgb(29, 155, 240);      // #1d9bf0
constexpr cef_color_t kAccent = Rgb(29, 155, 240);        // #1d9bf0
constexpr cef_color_t kText = Rgb(255, 255, 255);         // #ffffff
constexpr cef_color_t kMuted = Rgb(100, 100, 100);        // #646464
constexpr cef_color_t kCommand = Rgb(174, 214, 254);      // #aed6fe
constexpr cef_color_t kVimNormal = Rgb(72, 202, 228);     // #48cae4
constexpr cef_color_t kVimInsert = Rgb(46, 196, 182);     // #2ec4b6
constexpr cef_color_t kVimVisual = Rgb(199, 146, 234);    // #c792ea
constexpr cef_color_t kUserBg = Rgb(9, 13, 53);           // #090d35
constexpr cef_color_t kSidebarBg = Rgb(3, 8, 20);         // #030814
constexpr cef_color_t kSidebarSelBg = Rgb(15, 25, 60);    // #0f193c
constexpr cef_color_t kSelectionBg = Rgb(79, 82, 88);     // #4f5258
constexpr cef_color_t kSearchBg = Rgb(252, 224, 148);     // #fce094
constexpr cef_color_t kSearchFg = Rgb(0, 5, 15);          // #00050f
constexpr cef_color_t kSuccess = Rgb(80, 200, 120);       // #50c878
constexpr cef_color_t kBorderFocused = Rgb(28, 148, 229); // #1c94e5
constexpr cef_color_t kBorderUnfocused = Rgb(85, 85, 85); // #555555
constexpr cef_color_t kError = Rgb(255, 0, 0);
constexpr cef_color_t kWarning = Rgb(255, 255, 0);
constexpr cef_color_t kTool = Rgb(255, 0, 255);

}  // namespace vimbrowser::theme
