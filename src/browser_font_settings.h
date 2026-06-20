#pragma once

#include "include/cef_browser.h"

namespace vimbrowser {

inline void SetBrowserFontFamily(cef_string_t* target, const char* family) {
  CefString(target).FromString(family);
}

inline void ApplyBrowserFontSettings(CefBrowserSettings& settings) {
  // CEF's raw Blink defaults are Windows-centric (Arial, Times New Roman,
  // Courier New).  On Linux those families often are not installed in Skia's
  // font manager.  A page that writes a chain such as
  //
  //   "Helvetica Neue", Helvetica, Arial, sans-serif, fontello
  //
  // can then fall through to the later icon webfont for ordinary Latin text,
  // because the generic sans-serif preference itself resolves to unavailable
  // Arial.  Blink understands leading-comma preference strings as
  // first-available font lists (see GenericFontFamilySettings), so give each
  // generic family a Linux-native list before any page content is created.
  SetBrowserFontFamily(&settings.standard_font_family,
                       ",DejaVu Serif,Nimbus Roman,Liberation Serif,Times New Roman");
  SetBrowserFontFamily(&settings.serif_font_family,
                       ",DejaVu Serif,Nimbus Roman,Liberation Serif,Times New Roman");
  SetBrowserFontFamily(&settings.sans_serif_font_family,
                       ",DejaVu Sans,Nimbus Sans,Liberation Sans,Arial");
  SetBrowserFontFamily(&settings.fixed_font_family,
                       ",DejaVu Sans Mono,Nimbus Mono PS,Liberation Mono,Courier New");
}

}  // namespace vimbrowser
