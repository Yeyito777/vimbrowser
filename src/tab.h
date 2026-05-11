#pragma once

#include <string>

#include "browser_client.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_textfield.h"

namespace vimbrowser {

struct Tab {
  std::string url;
  CefRefPtr<BrowserClient> client;
  CefRefPtr<CefBrowserView> view;
  CefRefPtr<CefTextfield> label;
};

}  // namespace vimbrowser
