#include "browser_client.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include "browser_window.h"
#include "include/cef_app.h"
#include "include/cef_callback.h"
#include "include/cef_response.h"
#include "include/cef_response_filter.h"

extern "C" bool vimbrowser_browser_has_fps_sample(int browser_id);
extern "C" double vimbrowser_get_browser_fps(int browser_id);
extern "C" double vimbrowser_get_browser_refresh_rate(int browser_id);
extern "C" void vimbrowser_send_browser_command_key_event(
    int browser_id,
    const CefKeyEvent* event);

namespace vimbrowser {

struct BrowserClient::NetworkRequestRecord {
  mutable std::mutex mutex;
  uint64_t id = 0;
  uint64_t cef_request_id = 0;
  std::string url;
  std::string method;
  cef_resource_type_t resource_type = RT_SUB_RESOURCE;
  bool is_navigation = false;
  bool is_download = false;
  std::string request_initiator;
  std::vector<std::pair<std::string, std::string>> request_headers;
  std::string request_body;
  bool request_body_truncated = false;
  int status = 0;
  std::string status_text;
  std::string mime_type;
  std::string response_url;
  std::vector<std::pair<std::string, std::string>> response_headers;
  std::string response_body;
  bool response_body_truncated = false;
  cef_urlrequest_status_t request_status = UR_UNKNOWN;
  int64_t received_content_length = 0;
  bool completed = false;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point end = start;
};

namespace {

constexpr size_t kNetworkLogLimit = 1000;
constexpr size_t kNetworkBodyLimit = 1024 * 1024;
constexpr size_t kNetworkRequestBodyLimit = 256 * 1024;

bool ShouldOpenDispositionInTab(
    CefRequestHandler::WindowOpenDisposition disposition) {
  switch (disposition) {
    case CEF_WOD_SINGLETON_TAB:
    case CEF_WOD_NEW_FOREGROUND_TAB:
    case CEF_WOD_NEW_BACKGROUND_TAB:
    case CEF_WOD_NEW_POPUP:
    case CEF_WOD_NEW_WINDOW:
    case CEF_WOD_OFF_THE_RECORD:
    case CEF_WOD_SWITCH_TO_TAB:
      return true;
    default:
      return false;
  }
}

std::string JsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          constexpr char kHex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(kHex[(c >> 4) & 0xf]);
          out.push_back(kHex[c & 0xf]);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

std::string ResourceTypeName(cef_resource_type_t type) {
  switch (type) {
    case RT_MAIN_FRAME: return "main_frame";
    case RT_SUB_FRAME: return "sub_frame";
    case RT_STYLESHEET: return "stylesheet";
    case RT_SCRIPT: return "script";
    case RT_IMAGE: return "image";
    case RT_FONT_RESOURCE: return "font";
    case RT_SUB_RESOURCE: return "sub_resource";
    case RT_OBJECT: return "object";
    case RT_MEDIA: return "media";
    case RT_WORKER: return "worker";
    case RT_SHARED_WORKER: return "shared_worker";
    case RT_PREFETCH: return "prefetch";
    case RT_FAVICON: return "favicon";
    case RT_XHR: return "xhr";
    case RT_PING: return "ping";
    case RT_SERVICE_WORKER: return "service_worker";
    case RT_CSP_REPORT: return "csp_report";
    case RT_PLUGIN_RESOURCE: return "plugin_resource";
    default: return "unknown";
  }
}

std::string URLRequestStatusName(cef_urlrequest_status_t status) {
  switch (status) {
    case UR_UNKNOWN: return "unknown";
    case UR_SUCCESS: return "success";
    case UR_IO_PENDING: return "io_pending";
    case UR_CANCELED: return "canceled";
    case UR_FAILED: return "failed";
    default: return "unknown";
  }
}

std::vector<std::pair<std::string, std::string>> RequestHeaders(
    CefRefPtr<CefRequest> request) {
  std::vector<std::pair<std::string, std::string>> out;
  if (!request) {
    return out;
  }
  CefRequest::HeaderMap headers;
  request->GetHeaderMap(headers);
  for (const auto& [name, value] : headers) {
    out.emplace_back(name.ToString(), value.ToString());
  }
  return out;
}

std::vector<std::pair<std::string, std::string>> ResponseHeaders(
    CefRefPtr<CefResponse> response) {
  std::vector<std::pair<std::string, std::string>> out;
  if (!response) {
    return out;
  }
  CefResponse::HeaderMap headers;
  response->GetHeaderMap(headers);
  for (const auto& [name, value] : headers) {
    out.emplace_back(name.ToString(), value.ToString());
  }
  return out;
}

std::string HeadersJson(const std::vector<std::pair<std::string, std::string>>& headers) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < headers.size(); ++i) {
    if (i) {
      out << ",";
    }
    out << "{\"name\":\"" << JsonEscape(headers[i].first)
        << "\",\"value\":\"" << JsonEscape(headers[i].second) << "\"}";
  }
  out << "]";
  return out.str();
}

std::string PostDataPreview(CefRefPtr<CefPostData> post_data, bool* truncated) {
  if (truncated) {
    *truncated = false;
  }
  if (!post_data) {
    return {};
  }

  std::string body;
  CefPostData::ElementVector elements;
  post_data->GetElements(elements);
  for (CefRefPtr<CefPostDataElement> element : elements) {
    if (!element) {
      continue;
    }
    if (element->GetType() == PDE_TYPE_BYTES) {
      const size_t bytes = element->GetBytesCount();
      const size_t remaining = body.size() < kNetworkRequestBodyLimit
                                   ? kNetworkRequestBodyLimit - body.size()
                                   : 0;
      const size_t take = std::min(bytes, remaining);
      if (take > 0) {
        const size_t old_size = body.size();
        body.resize(old_size + take);
        element->GetBytes(take, body.data() + old_size);
      }
      if (take < bytes && truncated) {
        *truncated = true;
      }
    } else if (element->GetType() == PDE_TYPE_FILE) {
      const std::string file = element->GetFile().ToString();
      const std::string marker = "[file:" + file + "]";
      const size_t remaining = body.size() < kNetworkRequestBodyLimit
                                   ? kNetworkRequestBodyLimit - body.size()
                                   : 0;
      body.append(marker.data(), std::min(marker.size(), remaining));
      if (truncated) {
        *truncated = true;
      }
    }
  }
  if (post_data->HasExcludedElements() && truncated) {
    *truncated = true;
  }
  return body;
}

std::string RecordJson(const BrowserClient::NetworkRequestRecord& record,
                       bool detail) {
  std::lock_guard<std::mutex> lock(record.mutex);
  const double duration_ms = record.completed
                                 ? std::chrono::duration<double, std::milli>(
                                       record.end - record.start).count()
                                 : -1.0;
  std::ostringstream out;
  out << "{"
      << "\"id\":" << record.id << ","
      << "\"cef_request_id\":" << record.cef_request_id << ","
      << "\"url\":\"" << JsonEscape(record.url) << "\","
      << "\"method\":\"" << JsonEscape(record.method) << "\","
      << "\"resource_type\":\"" << ResourceTypeName(record.resource_type) << "\","
      << "\"resource_type_code\":" << static_cast<int>(record.resource_type) << ","
      << "\"request_initiator\":\"" << JsonEscape(record.request_initiator) << "\","
      << "\"is_navigation\":" << (record.is_navigation ? "true" : "false") << ","
      << "\"is_download\":" << (record.is_download ? "true" : "false") << ","
      << "\"status\":" << record.status << ","
      << "\"status_text\":\"" << JsonEscape(record.status_text) << "\","
      << "\"mime_type\":\"" << JsonEscape(record.mime_type) << "\","
      << "\"response_url\":\"" << JsonEscape(record.response_url) << "\","
      << "\"complete\":" << (record.completed ? "true" : "false") << ","
      << "\"request_status\":\"" << URLRequestStatusName(record.request_status) << "\","
      << "\"received_content_length\":" << record.received_content_length << ","
      << "\"body_size\":" << record.response_body.size() << ","
      << "\"body_truncated\":" << (record.response_body_truncated ? "true" : "false") << ","
      << "\"duration_ms\":" << duration_ms;
  if (detail) {
    out << ",\"request_headers\":" << HeadersJson(record.request_headers)
        << ",\"response_headers\":" << HeadersJson(record.response_headers)
        << ",\"request_body\":\"" << JsonEscape(record.request_body) << "\""
        << ",\"request_body_size\":" << record.request_body.size()
        << ",\"request_body_truncated\":"
        << (record.request_body_truncated ? "true" : "false");
  }
  out << "}";
  return out.str();
}

class CaptureResponseFilter final : public CefResponseFilter {
 public:
  explicit CaptureResponseFilter(
      std::shared_ptr<BrowserClient::NetworkRequestRecord> record)
      : record_(std::move(record)) {}

  bool InitFilter() override { return true; }

  FilterStatus Filter(void* data_in,
                      size_t data_in_size,
                      size_t& data_in_read,
                      void* data_out,
                      size_t data_out_size,
                      size_t& data_out_written) override {
    data_in_read = 0;
    data_out_written = 0;
    if (!data_in || data_in_size == 0) {
      return RESPONSE_FILTER_DONE;
    }
    if (!data_out || data_out_size == 0) {
      return RESPONSE_FILTER_NEED_MORE_DATA;
    }

    const size_t take = std::min(data_in_size, data_out_size);
    std::memcpy(data_out, data_in, take);
    data_in_read = take;
    data_out_written = take;

    if (record_) {
      std::lock_guard<std::mutex> lock(record_->mutex);
      const size_t remaining = record_->response_body.size() < kNetworkBodyLimit
                                   ? kNetworkBodyLimit - record_->response_body.size()
                                   : 0;
      const size_t capture = std::min(take, remaining);
      if (capture > 0) {
        record_->response_body.append(static_cast<const char*>(data_in), capture);
      }
      if (capture < take) {
        record_->response_body_truncated = true;
      }
    }

    return take == data_in_size ? RESPONSE_FILTER_DONE
                                : RESPONSE_FILTER_NEED_MORE_DATA;
  }

 private:
  std::shared_ptr<BrowserClient::NetworkRequestRecord> record_;

  IMPLEMENT_REFCOUNTING(CaptureResponseFilter);
  DISALLOW_COPY_AND_ASSIGN(CaptureResponseFilter);
};

}  // namespace

BrowserClient::BrowserClient(BrowserWindow* owner) : owner_(owner) {}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  if (owner_) {
    owner_->OnClientBrowserCreated(this);
  }
  std::cout << "vimbrowser: browser ready" << std::endl;
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> browser) {
  CefRefPtr<BrowserClient> keep_alive(this);
  if (owner_ && owner_->OnClientDoClose(this)) {
    return true;
  }
  return false;
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CefRefPtr<BrowserClient> keep_alive(this);
  browser_ = nullptr;
  if (owner_) {
    owner_->OnClientBeforeClose(this);
  }
}

void BrowserClient::OnBeforePopupAborted(CefRefPtr<CefBrowser> browser,
                                         int popup_id) {
  if (owner_) {
    owner_->OnClientBeforePopupAborted(this, popup_id);
  }
}

void BrowserClient::DetachOwner() {
  owner_ = nullptr;
}

void BrowserClient::OnLoadStart(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                TransitionType transition_type) {
  if (owner_ && frame && frame->IsMain()) {
    owner_->OnClientLoadStart(this, frame->GetURL().ToString());
  }
}

void BrowserClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                ErrorCode error_code,
                                const CefString& error_text,
                                const CefString& failed_url) {
  if (!frame->IsMain()) {
    return;
  }

  std::cerr << "vimbrowser: load failed: " << failed_url.ToString() << " "
            << error_text.ToString() << std::endl;
}

bool BrowserClient::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  int popup_id,
                                  const CefString& target_url,
                                  const CefString& target_frame_name,
                                  CefLifeSpanHandler::WindowOpenDisposition target_disposition,
                                  bool user_gesture,
                                  const CefPopupFeatures& popupFeatures,
                                  CefWindowInfo& windowInfo,
                                  CefRefPtr<CefClient>& client,
                                  CefBrowserSettings& settings,
                                  CefRefPtr<CefDictionaryValue>& extra_info,
                                  bool* no_javascript_access) {
  if (!owner_) {
    return true;
  }

  CefRefPtr<BrowserClient> popup_client = new BrowserClient(owner_);
  client = popup_client;
  const bool activate = target_disposition != CEF_WOD_NEW_BACKGROUND_TAB;
  return owner_->OnClientBeforePopup(this, popup_client, popup_id,
                                     target_url.ToString(), activate);
}

bool BrowserClient::OnOpenURLFromTab(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& target_url,
    CefRequestHandler::WindowOpenDisposition target_disposition,
    bool user_gesture) {
  if (!owner_) {
    return false;
  }
  if (!ShouldOpenDispositionInTab(target_disposition)) {
    return false;
  }

  const bool activate = target_disposition != CEF_WOD_NEW_BACKGROUND_TAB;
  return owner_->OnClientBeforePopup(this, nullptr, 0, target_url.ToString(),
                                     activate);
}

bool BrowserClient::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                     cef_log_severity_t level,
                                     const CefString& message,
                                     const CefString& source,
                                     int line) {
  const std::string text = message.ToString();
  if (!source.ToString().empty()) {
    return false;
  }

  constexpr std::string_view kOpenTabPrefix =
      "__vimbrowser_native_hint_open_tab__";
  if (text.rfind(kOpenTabPrefix, 0) == 0) {
    if (owner_) {
      owner_->OnNativeHintOpenTab(this, text.substr(kOpenTabPrefix.size()));
    }
    return true;
  }
  constexpr std::string_view kScrollTargetPrefix =
      "__vimbrowser_native_hint_scroll_target__";
  if (text.rfind(kScrollTargetPrefix, 0) == 0) {
    const std::string payload = text.substr(kScrollTargetPrefix.size());
    if (owner_) {
      char* end = nullptr;
      const long x = std::strtol(payload.c_str(), &end, 10);
      if (end && *end == ',') {
        char* y_end = nullptr;
        const long y = std::strtol(end + 1, &y_end, 10);
        if (y_end != end + 1) {
          bool is_page_scroller = false;
          if (*y_end == ',') {
            char* page_end = nullptr;
            const long page = std::strtol(y_end + 1, &page_end, 10);
            is_page_scroller = page_end != y_end + 1 && page != 0;
          }
          owner_->OnNativeHintScrollTarget(this, static_cast<int>(x),
                                          static_cast<int>(y),
                                          is_page_scroller);
        }
      }
    }
    return true;
  }
  if (text == "__vimbrowser_native_hints_stopped__") {
    if (owner_) {
      owner_->OnNativeHintsStopped(this);
    }
    return true;
  }
  return false;
}

bool BrowserClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                             CefRefPtr<CefFrame> frame,
                                             CefProcessId source_process,
                                             CefRefPtr<CefProcessMessage> message) {
  return owner_ && owner_->OnClientProcessMessage(this, browser, frame,
                                                  source_process, message);
}

bool BrowserClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                                  const CefKeyEvent& event,
                                  CefEventHandle os_event,
                                  bool* is_keyboard_shortcut) {
  if (owner_ && owner_->HandleBrowserKeyEvent(event)) {
    return true;
  }

  if (event.type != KEYEVENT_RAWKEYDOWN) {
    return false;
  }

  const bool ctrl = event.modifiers & EVENTFLAG_CONTROL_DOWN;
  const bool shift = event.modifiers & EVENTFLAG_SHIFT_DOWN;

  // First tiny native command surface. Ctrl+Shift+I opens DevTools just like
  // Chromium; this proves the CEF/CDP path is alive before we build the vim UI.
  if (ctrl && shift && event.windows_key_code == 'I') {
    ShowDevTools();
    return true;
  }

  return false;
}

CefRefPtr<CefResourceRequestHandler> BrowserClient::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool is_navigation,
    bool is_download,
    const CefString& request_initiator,
    bool& disable_default_handling) {
  disable_default_handling = false;
  if (!request) {
    return nullptr;
  }

  auto record = std::make_shared<NetworkRequestRecord>();
  record->cef_request_id = request->GetIdentifier();
  record->url = request->GetURL().ToString();
  record->method = request->GetMethod().ToString();
  record->resource_type = request->GetResourceType();
  record->is_navigation = is_navigation;
  record->is_download = is_download;
  record->request_initiator = request_initiator.ToString();
  record->request_headers = RequestHeaders(request);
  record->request_body = PostDataPreview(request->GetPostData(),
                                         &record->request_body_truncated);
  record->start = std::chrono::steady_clock::now();
  record->end = record->start;

  {
    std::lock_guard<std::mutex> lock(network_mutex_);
    record->id = next_network_request_id_++;
    network_log_.push_back(record);
    if (network_log_.size() > kNetworkLogLimit) {
      network_log_.erase(network_log_.begin(),
                         network_log_.begin() +
                             static_cast<std::ptrdiff_t>(network_log_.size() -
                                                         kNetworkLogLimit));
    }
    if (record->cef_request_id != 0) {
      active_network_by_cef_id_[record->cef_request_id] = record;
    }
  }

  return this;
}

BrowserClient::ReturnValue BrowserClient::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback> callback) {
  return RV_CONTINUE;
}

bool BrowserClient::OnResourceResponse(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       CefRefPtr<CefRequest> request,
                                       CefRefPtr<CefResponse> response) {
  if (auto record = request ? FindNetworkRecordByCefId(request->GetIdentifier())
                            : nullptr) {
    std::lock_guard<std::mutex> lock(record->mutex);
    record->response_headers = ResponseHeaders(response);
    if (response) {
      record->status = response->GetStatus();
      record->status_text = response->GetStatusText().ToString();
      record->mime_type = response->GetMimeType().ToString();
      record->response_url = response->GetURL().ToString();
    }
  }
  return false;
}

CefRefPtr<CefResponseFilter> BrowserClient::GetResourceResponseFilter(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefResponse> response) {
  auto record = request ? FindNetworkRecordByCefId(request->GetIdentifier())
                        : nullptr;
  if (!record) {
    return nullptr;
  }
  return new CaptureResponseFilter(record);
}

void BrowserClient::OnResourceLoadComplete(CefRefPtr<CefBrowser> browser,
                                           CefRefPtr<CefFrame> frame,
                                           CefRefPtr<CefRequest> request,
                                           CefRefPtr<CefResponse> response,
                                           URLRequestStatus status,
                                           int64_t received_content_length) {
  auto record = request ? FindNetworkRecordByCefId(request->GetIdentifier())
                        : nullptr;
  if (!record) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(record->mutex);
    record->request_status = status;
    record->received_content_length = received_content_length;
    record->completed = true;
    record->end = std::chrono::steady_clock::now();
    if (record->response_headers.empty()) {
      record->response_headers = ResponseHeaders(response);
    }
    if (response) {
      record->status = response->GetStatus();
      record->status_text = response->GetStatusText().ToString();
      record->mime_type = response->GetMimeType().ToString();
      record->response_url = response->GetURL().ToString();
    }
  }
  if (request && request->GetIdentifier() != 0) {
    std::lock_guard<std::mutex> lock(network_mutex_);
    active_network_by_cef_id_.erase(request->GetIdentifier());
  }
}

void BrowserClient::ShowDevTools() {
  if (!browser_) {
    return;
  }

  CefWindowInfo window_info;
  CefBrowserSettings settings;
  browser_->GetHost()->ShowDevTools(window_info, this, settings, CefPoint());
}

double BrowserClient::current_fps() const {
  return browser_ ? vimbrowser_get_browser_fps(browser_->GetIdentifier()) : 0.0;
}

bool BrowserClient::fps_has_sample() const {
  return browser_ && vimbrowser_browser_has_fps_sample(browser_->GetIdentifier());
}

double BrowserClient::compositor_refresh_rate() const {
  return browser_ ? vimbrowser_get_browser_refresh_rate(browser_->GetIdentifier())
                  : 0.0;
}

void BrowserClient::SendBrowserCommandKeyEvent(const CefKeyEvent& event) {
  if (browser_) {
    vimbrowser_send_browser_command_key_event(browser_->GetIdentifier(), &event);
  }
}

std::shared_ptr<BrowserClient::NetworkRequestRecord>
BrowserClient::FindNetworkRecord(uint64_t request_id) const {
  std::lock_guard<std::mutex> lock(network_mutex_);
  for (auto it = network_log_.rbegin(); it != network_log_.rend(); ++it) {
    if (*it && (*it)->id == request_id) {
      return *it;
    }
  }
  return nullptr;
}

std::shared_ptr<BrowserClient::NetworkRequestRecord>
BrowserClient::FindNetworkRecordByCefId(uint64_t cef_id) const {
  if (cef_id == 0) {
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(network_mutex_);
  auto it = active_network_by_cef_id_.find(cef_id);
  if (it != active_network_by_cef_id_.end()) {
    return it->second;
  }
  for (auto rit = network_log_.rbegin(); rit != network_log_.rend(); ++rit) {
    if (*rit && (*rit)->cef_request_id == cef_id) {
      return *rit;
    }
  }
  return nullptr;
}

std::string BrowserClient::NetworkListJson() const {
  std::vector<std::shared_ptr<NetworkRequestRecord>> records;
  {
    std::lock_guard<std::mutex> lock(network_mutex_);
    records = network_log_;
  }

  std::ostringstream out;
  out << "{\"requests\":[";
  for (size_t i = 0; i < records.size(); ++i) {
    if (i) {
      out << ",";
    }
    out << RecordJson(*records[i], false);
  }
  out << "]}";
  return out.str();
}

std::string BrowserClient::NetworkDetailJson(uint64_t request_id) const {
  auto record = FindNetworkRecord(request_id);
  if (!record) {
    return "ERR no such request\n";
  }
  return RecordJson(*record, true);
}

bool BrowserClient::NetworkBody(uint64_t request_id,
                                std::string* body,
                                std::string* error) const {
  auto record = FindNetworkRecord(request_id);
  if (!record) {
    if (error) *error = "ERR no such request\n";
    return false;
  }
  std::lock_guard<std::mutex> lock(record->mutex);
  if (record->response_body.empty() && !record->completed) {
    if (error) *error = "ERR response body not available yet\n";
    return false;
  }
  if (body) {
    *body = record->response_body;
  }
  return true;
}

void BrowserClient::ClearNetworkLog() {
  std::lock_guard<std::mutex> lock(network_mutex_);
  active_network_by_cef_id_.clear();
  network_log_.clear();
}

CefRefPtr<CefRequest> BrowserClient::BuildReplayRequest(uint64_t request_id,
                                                        std::string* error) const {
  auto record = FindNetworkRecord(request_id);
  if (!record) {
    if (error) *error = "ERR no such request\n";
    return nullptr;
  }

  std::string url;
  std::string method;
  std::string body;
  bool request_body_truncated = false;
  std::vector<std::pair<std::string, std::string>> headers;
  {
    std::lock_guard<std::mutex> lock(record->mutex);
    url = record->url;
    method = record->method.empty() ? "GET" : record->method;
    body = record->request_body;
    request_body_truncated = record->request_body_truncated;
    headers = record->request_headers;
  }
  if (url.empty()) {
    if (error) *error = "ERR request has no url\n";
    return nullptr;
  }
  if (request_body_truncated) {
    if (error) *error = "ERR request body was truncated; cannot replay safely\n";
    return nullptr;
  }

  CefRequest::HeaderMap header_map;
  for (const auto& [name, value] : headers) {
    const std::string lower_name = [&] {
      std::string lower = name;
      std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return lower;
    }();
    if (lower_name == "host" || lower_name == "content-length") {
      continue;
    }
    header_map.insert({name, value});
  }

  CefRefPtr<CefPostData> post_data;
  if (!body.empty()) {
    CefRefPtr<CefPostDataElement> element = CefPostDataElement::Create();
    element->SetToBytes(body.size(), body.data());
    post_data = CefPostData::Create();
    post_data->AddElement(element);
  }

  CefRefPtr<CefRequest> request = CefRequest::Create();
  request->Set(url, method, post_data, header_map);
  request->SetFlags(UR_FLAG_ALLOW_STORED_CREDENTIALS);
  request->SetFirstPartyForCookies(url);
  return request;
}

}  // namespace vimbrowser
