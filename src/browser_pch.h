#pragma once

// Stable, heavy C++ headers shared by the native shell.  CMake injects this
// only for C++ translation units; keep C sources such as shortcuts.c out of the
// precompiled-header path.

#ifdef __cplusplus

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_color_ids.h"
#include "include/cef_cookie.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_navigation_entry.h"
#include "include/cef_parser.h"
#include "include/cef_process_message.h"
#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/cef_string_visitor.h"
#include "include/cef_urlrequest.h"
#include "include/cef_values.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_button.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_fill_layout.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"

#endif  // __cplusplus
