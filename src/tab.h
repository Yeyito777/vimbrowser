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
};

}  // namespace vimbrowser
