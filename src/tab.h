#pragma once

#include <cstdint>
#include <string>

#include "browser_client.h"
#include "include/views/cef_browser_view.h"

namespace vimbrowser {

struct Tab {
  uint64_t id = 0;
  std::string id_json;
  std::string url;
  std::string url_json;
  CefRefPtr<BrowserClient> client;
  CefRefPtr<CefBrowserView> view;
  bool deferred_load = false;
  bool audible = false;
  bool focused_editable_node = false;
  bool has_scroll_target = false;
  int scroll_target_x = 0;
  int scroll_target_y = 0;
  bool scroll_target_is_page = true;
  bool scroll_target_is_pdf_viewport = false;
};

}  // namespace vimbrowser
