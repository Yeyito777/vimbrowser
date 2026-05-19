#pragma once

#include <cstdint>
#include <string>

#include "browser_client.h"
#include "include/views/cef_browser_view.h"

namespace vimbrowser {

struct Tab {
  uint64_t id = 0;
  std::string url;
  CefRefPtr<BrowserClient> client;
  CefRefPtr<CefBrowserView> view;
  bool has_scroll_target = false;
  int scroll_target_x = 0;
  int scroll_target_y = 0;
  bool scroll_target_is_page = true;
};

}  // namespace vimbrowser
