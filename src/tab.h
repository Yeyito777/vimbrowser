#pragma once

#include <string>

#include "browser_client.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_label_button.h"

namespace vimbrowser {

struct Tab {
  std::string url;
  CefRefPtr<BrowserClient> client;
  CefRefPtr<CefBrowserView> view;
  CefRefPtr<CefLabelButton> label;
};

}  // namespace vimbrowser
