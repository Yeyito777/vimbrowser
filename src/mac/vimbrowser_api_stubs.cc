// The Linux build links a patched libcef that exports these vimbrowser_*
// functions (see backend/chromium/cef/libcef/browser/vimbrowser_browser_api.cc).
// Stock CEF binary distributions used for the macOS build do not implement
// them, so the shell degrades gracefully: no FPS samples, no audible-tab
// sidebar indicator, and no native Blink hint dispatch (f / F / Ctrl+l /
// Ctrl+h / Ctrl+Space hints are implemented inside the patched Blink tree and
// are unavailable against stock CEF).
#include "include/internal/cef_types_wrappers.h"

extern "C" bool vimbrowser_browser_has_fps_sample(int /*browser_id*/) {
  return false;
}

extern "C" double vimbrowser_get_browser_fps(int /*browser_id*/) {
  return 0.0;
}

extern "C" double vimbrowser_get_browser_refresh_rate(int /*browser_id*/) {
  return 0.0;
}

extern "C" bool vimbrowser_browser_is_currently_audible(int /*browser_id*/) {
  return false;
}

extern "C" void vimbrowser_send_browser_command_key_event(
    int /*browser_id*/,
    const CefKeyEvent* /*event*/) {
  // Intentionally a no-op: forwarding the synthetic hint-trigger keys to a
  // stock renderer would type stray characters into the page instead of
  // starting hint mode.
}
