#include "include/cef_browser.h"

// The normal desktop build links these functions from vimbrowser's customized
// Chromium/CEF backend.  The A26 bring-up intentionally starts with the exact
// matching official ARM64 CEF binary so that we can prove the phone runtime
// without paying for a second full Chromium build.  Features backed by these
// hooks are unavailable in that compatibility build; normal CEF page rendering,
// navigation, DevTools and vimbrowser IPC remain available.
extern "C" bool vimbrowser_browser_has_fps_sample(int) {
  return false;
}

extern "C" double vimbrowser_get_browser_fps(int) {
  return 0.0;
}

extern "C" double vimbrowser_get_browser_refresh_rate(int) {
  return 0.0;
}

extern "C" bool vimbrowser_browser_is_currently_audible(int) {
  return false;
}

extern "C" void vimbrowser_send_browser_command_key_event(
    int,
    const CefKeyEvent*) {}
