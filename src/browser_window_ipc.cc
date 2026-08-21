#include "browser_window.h"
#include "browser_window_internal.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "browser_client.h"
#include "config.h"
#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "include/cef_cookie.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_navigation_entry.h"
#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/cef_string_visitor.h"
#include "include/cef_urlrequest.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_closure_task.h"

extern "C" bool vimbrowser_frame_is_out_of_process(
    int browser_id,
    const char* frame_identifier,
    size_t frame_identifier_size);
extern "C" bool vimbrowser_inspect_frame_controls(
    int browser_id,
    const char* frame_identifier,
    size_t frame_identifier_size,
    const char* role,
    size_t role_size,
    const char* exact_name,
    size_t exact_name_size,
    const char* context_contains,
    size_t context_contains_size,
    uint32_t limit,
    void (*callback)(void* user_data, int result, const char* json,
                     size_t json_size),
    void* user_data);
extern "C" bool vimbrowser_activate_element_handle(
    int browser_id,
    const char* handle,
    size_t handle_size,
    bool grant_user_activation,
    uint64_t* activation_nonce_high,
    uint64_t* activation_nonce_low,
    void (*callback)(void* user_data, int result, int match_count),
    void* user_data);

namespace vimbrowser {
namespace {

constexpr size_t kMaxJsFileBytes = 1024 * 1024;
constexpr size_t kMaxUploadFiles = 32;
constexpr size_t kMaxUploadSelectorBytes = 4096;
constexpr size_t kMaxUploadPathBytes = 4096;
constexpr size_t kMaxUploadPayloadBytes = 256 * 1024;

bool DecodeBase64JsPayload(const std::string& encoded,
                           std::string* code,
                           std::string* error) {
  if (!code || !error || encoded.empty() ||
      encoded.size() > ((kMaxJsFileBytes + 2) / 3) * 4 + 4) {
    if (error) *error = "JavaScript payload is missing or too large";
    return false;
  }
  CefRefPtr<CefBinaryValue> decoded = CefBase64Decode(encoded);
  if (!decoded || decoded->GetSize() == 0 ||
      decoded->GetSize() > kMaxJsFileBytes) {
    *error = "JavaScript payload is not valid base64";
    return false;
  }
  std::vector<char> bytes(decoded->GetSize());
  if (decoded->GetData(bytes.data(), bytes.size(), 0) != bytes.size()) {
    *error = "JavaScript payload could not be decoded";
    return false;
  }
  code->assign(bytes.data(), bytes.size());
  return true;
}
constexpr int kAutomaticUploadChooserTimeoutMs = 3000;

// Keep in sync with blink.mojom.VimbrowserElementActivationResult. This is a
// deliberately tiny private C ABI between the shell and its customized CEF
// backend, avoiding generated CEF API churn for a vimbrowser-only operation.
enum class VimbrowserElementActivationResult {
  kDispatched = 0,
  kDocumentUnavailable = 1,
  kInvalidSelector = 2,
  kTargetNotFound = 3,
  kAmbiguousTarget = 4,
  kTargetNotVisible = 5,
  kTargetObscured = 6,
  kActivationIgnored = 7,
  kBackendUnavailable = 8,
  kInvalidHandle = 9,
  kExpiredHandle = 10,
  kStaleFrame = 11,
  kStaleDocument = 12,
  kStaleNode = 13,
  kTargetDisabled = 14,
};

struct VimbrowserElementActivationError {
  std::string_view code;
  std::string_view message;
};

VimbrowserElementActivationError ElementActivationErrorForResult(
    VimbrowserElementActivationResult result) {
  switch (result) {
    case VimbrowserElementActivationResult::kDocumentUnavailable:
      return {"document_unavailable",
              "tab document is unavailable for native activation"};
    case VimbrowserElementActivationResult::kInvalidSelector:
      return {"invalid_selector",
              "activation target is not a valid CSS selector"};
    case VimbrowserElementActivationResult::kTargetNotFound:
      return {"target_not_found", "activation target was not found"};
    case VimbrowserElementActivationResult::kAmbiguousTarget:
      return {"ambiguous_target",
              "activation target resolved to more than one element"};
    case VimbrowserElementActivationResult::kTargetNotVisible:
      return {"target_not_visible",
              "activation target is not visible in the tab viewport"};
    case VimbrowserElementActivationResult::kTargetObscured:
      return {"target_obscured",
              "activation target is covered or not the native hit-test target"};
    case VimbrowserElementActivationResult::kActivationIgnored:
      return {"activation_ignored",
              "Blink did not dispatch the requested element activation"};
    case VimbrowserElementActivationResult::kInvalidHandle:
      return {"invalid_handle",
              "inspected element handle is unknown or already consumed"};
    case VimbrowserElementActivationResult::kExpiredHandle:
      return {"expired_handle",
              "inspected element handle expired before activation"};
    case VimbrowserElementActivationResult::kStaleFrame:
      return {"stale_frame", "inspected element frame is no longer active"};
    case VimbrowserElementActivationResult::kStaleDocument:
      return {"stale_document",
              "inspected element document changed before activation"};
    case VimbrowserElementActivationResult::kStaleNode:
      return {"stale_node",
              "inspected element was removed or replaced before activation"};
    case VimbrowserElementActivationResult::kTargetDisabled:
      return {"target_disabled",
              "inspected element became disabled before activation"};
    case VimbrowserElementActivationResult::kDispatched:
      return {"", ""};
    case VimbrowserElementActivationResult::kBackendUnavailable:
    default:
      return {"activation_backend_unavailable",
              "custom Chromium element activation backend became unavailable"};
  }
}

struct UploadFileRequest {
  uint64_t tab_id = 0;
  std::string target_kind;
  std::string selector;
  std::string handle;
  int input_index = -1;
  std::vector<std::string> paths;
};

struct InspectControlsRequest {
  std::string frame_id;
  std::string role;
  std::string exact_name;
  std::string context_contains;
  uint32_t limit = 100;
};

struct UploadFileValidation {
  bool ok = false;
  std::string error;
  std::string error_code;
  std::string error_message;
  int error_file_index = -1;
  std::vector<std::string> canonical_paths;
};

std::string UploadFileErrorJson(std::string_view code,
                                std::string_view message,
                                int file_index = -1,
                                int match_count = -1) {
  std::ostringstream out;
  out << "{\"ok\":false,\"error\":{\"code\":\"" << JsonEscape(code)
      << "\",\"message\":\"" << JsonEscape(message) << "\"";
  if (file_index >= 0) {
    out << ",\"file_index\":" << file_index;
  }
  if (match_count >= 0) {
    out << ",\"match_count\":" << match_count;
  }
  out << "}}";
  return out.str();
}

bool DecodeUploadFilePayload(const std::string& encoded,
                             UploadFileRequest* request,
                             std::string* error) {
  auto fail = [error](std::string_view code, std::string_view message) {
    if (error) {
      *error = UploadFileErrorJson(code, message);
    }
    return false;
  };
  if (!request || encoded.empty() ||
      encoded.size() > ((kMaxUploadPayloadBytes + 2) / 3) * 4 + 4) {
    return fail("invalid_payload", "upload payload is missing or too large");
  }

  CefRefPtr<CefBinaryValue> decoded = CefBase64Decode(encoded);
  if (!decoded || decoded->GetSize() == 0 ||
      decoded->GetSize() > kMaxUploadPayloadBytes) {
    return fail("invalid_payload", "upload payload is not valid base64");
  }
  std::vector<char> bytes(decoded->GetSize());
  if (decoded->GetData(bytes.data(), bytes.size(), 0) != bytes.size()) {
    return fail("invalid_payload", "upload payload could not be decoded");
  }
  CefRefPtr<CefValue> value =
      CefParseJSON(bytes.data(), bytes.size(), JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY) {
    return fail("invalid_payload", "upload payload is not a JSON object");
  }
  CefRefPtr<CefDictionaryValue> root = value->GetDictionary();
  if (!root || root->GetType("version") != VTYPE_INT ||
      root->GetInt("version") != 1 ||
      root->GetType("target") != VTYPE_DICTIONARY ||
      root->GetType("paths") != VTYPE_LIST) {
    return fail("invalid_payload", "upload payload has an unsupported schema");
  }

  CefRefPtr<CefDictionaryValue> target = root->GetDictionary("target");
  if (!target || target->GetType("kind") != VTYPE_STRING) {
    return fail("invalid_target", "upload target is missing its kind");
  }
  request->target_kind = ToLowerAscii(target->GetString("kind").ToString());
  if (request->target_kind == "css" ||
      request->target_kind == "activate") {
    if (target->GetType("value") != VTYPE_STRING) {
      return fail("invalid_target", "CSS upload target must be a string");
    }
    request->selector = target->GetString("value").ToString();
    if (request->selector.empty() ||
        request->selector.size() > kMaxUploadSelectorBytes) {
      return fail("invalid_target", "CSS upload target is empty or too long");
    }
  } else if (request->target_kind == "index") {
    if (target->GetType("value") != VTYPE_INT) {
      return fail("invalid_target", "upload input index must be an integer");
    }
    request->input_index = target->GetInt("value");
    if (request->input_index < 0 || request->input_index > 10000) {
      return fail("invalid_target", "upload input index is out of range");
    }
  } else if (request->target_kind == "chooser") {
    // The target is the next browser-native open-file chooser from this tab.
    // There is deliberately no selector or renderer-provided path in this mode.
  } else if (request->target_kind == "handle") {
    if (target->GetType("value") != VTYPE_STRING) {
      return fail("invalid_target", "inspected target handle must be a string");
    }
    request->handle = target->GetString("value").ToString();
    if (!request->handle.starts_with("eh1_") ||
        request->handle.size() > 128) {
      return fail("invalid_target", "inspected target handle is malformed");
    }
  } else {
    return fail("invalid_target",
                "upload target kind must be css, index, activate, handle, or chooser");
  }

  CefRefPtr<CefListValue> paths = root->GetList("paths");
  if (!paths || paths->GetSize() == 0 || paths->GetSize() > kMaxUploadFiles) {
    return fail("invalid_path_count", "upload requires between 1 and 32 files");
  }
  request->paths.reserve(paths->GetSize());
  for (size_t i = 0; i < paths->GetSize(); ++i) {
    if (paths->GetType(i) != VTYPE_STRING) {
      return fail("invalid_path", "every upload path must be a string");
    }
    std::string path = paths->GetString(i).ToString();
    if (path.empty() || path.size() > kMaxUploadPathBytes) {
      return fail("invalid_path", "an upload path is empty or too long");
    }
    request->paths.push_back(std::move(path));
  }
  return true;
}

bool DecodeInspectControlsPayload(const std::string& encoded,
                                  InspectControlsRequest* request,
                                  std::string* error) {
  auto fail = [error](std::string_view code, std::string_view message) {
    if (error) {
      *error = UploadFileErrorJson(code, message);
    }
    return false;
  };
  if (!request || encoded.empty() || encoded.size() > 16384) {
    return fail("invalid_payload", "inspection payload is missing or too large");
  }
  CefRefPtr<CefBinaryValue> decoded = CefBase64Decode(encoded);
  if (!decoded || decoded->GetSize() == 0 || decoded->GetSize() > 8192) {
    return fail("invalid_payload", "inspection payload is not valid base64");
  }
  std::vector<char> bytes(decoded->GetSize());
  if (decoded->GetData(bytes.data(), bytes.size(), 0) != bytes.size()) {
    return fail("invalid_payload", "inspection payload could not be decoded");
  }
  CefRefPtr<CefValue> value =
      CefParseJSON(bytes.data(), bytes.size(), JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_DICTIONARY) {
    return fail("invalid_payload", "inspection payload is not a JSON object");
  }
  CefRefPtr<CefDictionaryValue> root = value->GetDictionary();
  if (!root || root->GetType("version") != VTYPE_INT ||
      root->GetInt("version") != 1 ||
      root->GetType("frame_id") != VTYPE_STRING ||
      root->GetType("filter") != VTYPE_DICTIONARY) {
    return fail("invalid_payload", "inspection payload has an unsupported schema");
  }
  request->frame_id = root->GetString("frame_id").ToString();
  if (request->frame_id.empty() || request->frame_id.size() > 256) {
    return fail("invalid_frame", "inspection frame identifier is invalid");
  }
  CefRefPtr<CefDictionaryValue> filter = root->GetDictionary("filter");
  auto optional_string = [&](const char* key, size_t max,
                             std::string* output) -> bool {
    if (!filter->HasKey(key)) {
      output->clear();
      return true;
    }
    if (filter->GetType(key) != VTYPE_STRING) {
      return false;
    }
    *output = filter->GetString(key).ToString();
    return output->size() <= max;
  };
  if (!optional_string("role", 128, &request->role) ||
      !optional_string("exact_name", 256, &request->exact_name) ||
      !optional_string("context_contains", 512,
                       &request->context_contains)) {
    return fail("invalid_filter", "inspection filter is invalid or too long");
  }
  if (root->HasKey("limit")) {
    if (root->GetType("limit") != VTYPE_INT || root->GetInt("limit") < 1 ||
        root->GetInt("limit") > 100) {
      return fail("invalid_limit", "inspection limit must be between 1 and 100");
    }
    request->limit = static_cast<uint32_t>(root->GetInt("limit"));
  }
  return true;
}

UploadFileValidation ValidateUploadFilePaths(
    const std::vector<std::string>& paths) {
  UploadFileValidation result;
  auto fail = [&result](std::string code,
                        std::string message,
                        int file_index) -> UploadFileValidation {
    result.error_code = std::move(code);
    result.error_message = std::move(message);
    result.error_file_index = file_index;
    result.error = UploadFileErrorJson(result.error_code, result.error_message,
                                       result.error_file_index);
    return result;
  };
  result.canonical_paths.reserve(paths.size());
  for (size_t i = 0; i < paths.size(); ++i) {
    const std::filesystem::path input(paths[i]);
    if (!input.is_absolute()) {
      return fail("path_not_absolute", "upload file path must be absolute",
                  static_cast<int>(i));
    }

    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::canonical(input, ec);
    if (ec || canonical.empty()) {
      return fail("path_not_found", "upload file path does not exist",
                  static_cast<int>(i));
    }
    const std::string canonical_text = canonical.string();
    if (canonical_text.size() > kMaxUploadPathBytes) {
      return fail("path_too_long", "canonical upload file path is too long",
                  static_cast<int>(i));
    }

    // Open nonblocking and inspect the descriptor so FIFOs/devices cannot wedge
    // the browser and a path swap cannot make a special file pass the check.
    const int fd = open(canonical_text.c_str(),
                        O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0) {
      return fail("path_not_readable", "upload file path is not readable",
                  static_cast<int>(i));
    }
    struct stat info = {};
    const bool inspected = fstat(fd, &info) == 0;
    close(fd);
    if (!inspected || !S_ISREG(info.st_mode)) {
      return fail("path_not_regular", "upload file path is not a regular file",
                  static_cast<int>(i));
    }
    result.canonical_paths.push_back(canonical_text);
  }
  result.ok = true;
  return result;
}

std::vector<std::string> SplitCommaSeparated(std::string_view value) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= value.size()) {
    const size_t comma = value.find(',', start);
    const size_t end = comma == std::string_view::npos ? value.size() : comma;
    parts.push_back(Trim(std::string(value.substr(start, end - start))));
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return parts;
}

bool UploadFileMatchesAccept(const std::string& path,
                             const std::string& accept) {
  if (Trim(accept).empty()) {
    return true;
  }
  std::string extension =
      ToLowerAscii(std::filesystem::path(path).extension().string());
  std::string mime;
  if (extension.size() > 1) {
    mime = ToLowerAscii(CefGetMimeType(extension.substr(1)).ToString());
  }

  bool had_valid_constraint = false;
  for (std::string token : SplitCommaSeparated(accept)) {
    token = ToLowerAscii(std::move(token));
    if (token.size() > 1 && token.front() == '.') {
      had_valid_constraint = true;
      if (extension == token) {
        return true;
      }
      continue;
    }
    const size_t slash = token.find('/');
    if (slash == std::string::npos || slash == 0 || slash + 1 >= token.size()) {
      continue;  // Browsers ignore invalid accept tokens.
    }
    had_valid_constraint = true;
    if (token.ends_with("/*")) {
      const std::string prefix = token.substr(0, token.size() - 1);
      if (!mime.empty() && mime.starts_with(prefix)) {
        return true;
      }
    } else if (!mime.empty() && mime == token) {
      return true;
    }
  }
  return !had_valid_constraint;
}

std::string FileChooserAcceptConstraint(
    const std::vector<CefString>& accept_filters,
    const std::vector<CefString>& accept_extensions) {
  std::string constraint;
  auto append = [&constraint](std::string value) {
    std::replace(value.begin(), value.end(), ';', ',');
    if (value.empty()) {
      return;
    }
    if (!constraint.empty()) {
      constraint.push_back(',');
    }
    constraint += value;
  };
  for (const CefString& filter : accept_filters) {
    append(filter.ToString());
  }
  if (constraint.empty()) {
    for (const CefString& extensions : accept_extensions) {
      append(extensions.ToString());
    }
  }
  return constraint;
}

int NextUploadFileDevToolsMessageId() {
  static int next_message_id = 800000000;
  if (next_message_id >= 899000000) {
    next_message_id = 800000000;
  }
  return next_message_id++;
}

class UploadFileDevToolsObserver final : public CefDevToolsMessageObserver {
 public:
  UploadFileDevToolsObserver(CefRefPtr<CefBrowserHost> host,
                             UploadFileRequest request,
                             IpcReplyCallback reply)
      : host_(std::move(host)),
        request_(std::move(request)),
        reply_(std::move(reply)) {}

  void Start() {
    if (!host_) {
      Finish(UploadFileErrorJson("no_browser_host", "tab has no browser host"));
      return;
    }
    registration_ = host_->AddDevToolsMessageObserver(this);
    state_ = State::kGetDocument;
    CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
    params->SetInt("depth", 0);
    params->SetBool("pierce", true);
    Send("DOM.getDocument", params);
  }

  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser,
                              int message_id,
                              bool success,
                              const void* result,
                              size_t result_size) override {
    if (completed_ || message_id == 0 || message_id != message_id_) {
      return;
    }
    if (!success) {
      const std::string code = state_ == State::kQuery &&
                                       request_.target_kind == "css"
                                   ? "invalid_selector"
                                   : "devtools_error";
      Finish(UploadFileErrorJson(
          code, state_ == State::kSetFiles
                    ? "browser rejected the file-input assignment"
                    : "browser could not resolve the file-input target"));
      return;
    }

    CefRefPtr<CefDictionaryValue> dict;
    if (result && result_size > 0) {
      CefRefPtr<CefValue> value =
          CefParseJSON(result, result_size, JSON_PARSER_RFC);
      if (!value || value->GetType() != VTYPE_DICTIONARY) {
        Finish(UploadFileErrorJson("invalid_browser_response",
                                   "browser returned invalid target metadata"));
        return;
      }
      dict = value->GetDictionary();
    }
    if (!dict) {
      dict = CefDictionaryValue::Create();
    }

    switch (state_) {
      case State::kGetDocument:
        HandleDocument(dict);
        break;
      case State::kQuery:
        HandleQuery(dict);
        break;
      case State::kDescribe:
        HandleDescribe(dict);
        break;
      case State::kSetFiles:
        FinishSuccess();
        break;
      case State::kDone:
        break;
    }
  }

  void Timeout() {
    Finish(UploadFileErrorJson("timeout", "file-input assignment timed out"));
  }

 private:
  enum class State { kGetDocument, kQuery, kDescribe, kSetFiles, kDone };

  void Send(const CefString& method, CefRefPtr<CefDictionaryValue> params) {
    if (completed_ || !host_) {
      return;
    }
    const int requested_id = NextUploadFileDevToolsMessageId();
    message_id_ = requested_id;
    const int actual_id = host_->ExecuteDevToolsMethod(requested_id, method, params);
    if (actual_id == 0) {
      Finish(UploadFileErrorJson("devtools_start_failed",
                                 "browser could not start file-input assignment"));
    } else {
      message_id_ = actual_id;
    }
  }

  void HandleDocument(CefRefPtr<CefDictionaryValue> result) {
    if (result->GetType("root") != VTYPE_DICTIONARY) {
      Finish(UploadFileErrorJson("document_unavailable",
                                 "tab document is not available"));
      return;
    }
    CefRefPtr<CefDictionaryValue> root = result->GetDictionary("root");
    if (!root || root->GetType("nodeId") != VTYPE_INT ||
        root->GetInt("nodeId") <= 0) {
      Finish(UploadFileErrorJson("document_unavailable",
                                 "tab document has no DOM root"));
      return;
    }
    state_ = State::kQuery;
    CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
    params->SetInt("nodeId", root->GetInt("nodeId"));
    params->SetString("selector", request_.target_kind == "css"
                                      ? request_.selector
                                      : "input[type=\"file\"]");
    Send("DOM.querySelectorAll", params);
  }

  void HandleQuery(CefRefPtr<CefDictionaryValue> result) {
    if (result->GetType("nodeIds") != VTYPE_LIST) {
      Finish(UploadFileErrorJson("invalid_browser_response",
                                 "browser returned no target matches"));
      return;
    }
    CefRefPtr<CefListValue> nodes = result->GetList("nodeIds");
    const int count = nodes ? static_cast<int>(nodes->GetSize()) : 0;
    match_count_ = count;
    int selected = -1;
    if (request_.target_kind == "css") {
      if (count == 0) {
        Finish(UploadFileErrorJson("target_not_found",
                                   "CSS target did not match an element", -1, 0));
        return;
      }
      if (count != 1) {
        Finish(UploadFileErrorJson(
            "ambiguous_target", "CSS target matched more than one element", -1,
            count));
        return;
      }
      selected = nodes->GetInt(0);
    } else {
      if (request_.input_index >= count) {
        Finish(UploadFileErrorJson(
            "input_index_out_of_range", "file-input index is out of range", -1,
            count));
        return;
      }
      selected = nodes->GetInt(static_cast<size_t>(request_.input_index));
    }
    if (selected <= 0) {
      Finish(UploadFileErrorJson("invalid_browser_response",
                                 "browser returned an invalid target node"));
      return;
    }
    node_id_ = selected;
    state_ = State::kDescribe;
    CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
    params->SetInt("nodeId", node_id_);
    params->SetInt("depth", 0);
    Send("DOM.describeNode", params);
  }

  void HandleDescribe(CefRefPtr<CefDictionaryValue> result) {
    if (result->GetType("node") != VTYPE_DICTIONARY) {
      Finish(UploadFileErrorJson("invalid_browser_response",
                                 "browser returned no target node metadata"));
      return;
    }
    CefRefPtr<CefDictionaryValue> node = result->GetDictionary("node");
    if (!node || node->GetType("nodeName") != VTYPE_STRING) {
      Finish(UploadFileErrorJson("invalid_browser_response",
                                 "browser returned incomplete target metadata"));
      return;
    }

    std::map<std::string, std::string> attributes;
    if (node->GetType("attributes") == VTYPE_LIST) {
      CefRefPtr<CefListValue> list = node->GetList("attributes");
      if (list) {
        for (size_t i = 0; i + 1 < list->GetSize(); i += 2) {
          if (list->GetType(i) == VTYPE_STRING &&
              list->GetType(i + 1) == VTYPE_STRING) {
            attributes[ToLowerAscii(list->GetString(i).ToString())] =
                list->GetString(i + 1).ToString();
          }
        }
      }
    }
    const auto type = attributes.find("type");
    if (ToLowerAscii(node->GetString("nodeName").ToString()) != "input" ||
        type == attributes.end() || ToLowerAscii(type->second) != "file") {
      Finish(UploadFileErrorJson(
          "target_not_file_input", "target is not an input of type file"));
      return;
    }

    multiple_ = attributes.contains("multiple");
    const auto accept = attributes.find("accept");
    accept_ = accept == attributes.end() ? std::string() : accept->second;
    if (!multiple_ && request_.paths.size() > 1) {
      Finish(UploadFileErrorJson(
          "multiple_not_allowed", "target file input does not allow multiple files"));
      return;
    }
    for (size_t i = 0; i < request_.paths.size(); ++i) {
      if (!UploadFileMatchesAccept(request_.paths[i], accept_)) {
        Finish(UploadFileErrorJson(
            "accept_mismatch", "upload file does not match the input accept constraint",
            static_cast<int>(i)));
        return;
      }
    }

    CefRefPtr<CefListValue> files = CefListValue::Create();
    files->SetSize(request_.paths.size());
    for (size_t i = 0; i < request_.paths.size(); ++i) {
      files->SetString(i, request_.paths[i]);
    }
    state_ = State::kSetFiles;
    CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
    params->SetInt("nodeId", node_id_);
    params->SetList("files", files);
    Send("DOM.setFileInputFiles", params);
  }

  void FinishSuccess() {
    std::ostringstream out;
    out << "{\"ok\":true,\"tabid\":" << request_.tab_id
        << ",\"file_count\":" << request_.paths.size()
        << ",\"target\":{\"kind\":\"" << request_.target_kind
        << "\",\"match_count\":" << match_count_;
    if (request_.target_kind == "index") {
      out << ",\"index\":" << request_.input_index;
    }
    out << "},\"input\":{\"multiple\":" << (multiple_ ? "true" : "false")
        << ",\"accept\":\"" << JsonEscape(accept_) << "\"}}";
    Finish(out.str());
  }

  void Finish(std::string response) {
    if (completed_) {
      return;
    }
    completed_ = true;
    state_ = State::kDone;
    registration_ = nullptr;
    host_ = nullptr;
    request_.paths.clear();
    if (reply_) {
      auto reply = std::move(reply_);
      reply(std::move(response));
    }
  }

  CefRefPtr<CefBrowserHost> host_;
  UploadFileRequest request_;
  IpcReplyCallback reply_;
  CefRefPtr<CefRegistration> registration_;
  State state_ = State::kGetDocument;
  int message_id_ = 0;
  int node_id_ = 0;
  int match_count_ = 0;
  bool multiple_ = false;
  bool completed_ = false;
  std::string accept_;

  IMPLEMENT_REFCOUNTING(UploadFileDevToolsObserver);
  DISALLOW_COPY_AND_ASSIGN(UploadFileDevToolsObserver);
};

void StartUploadFileAssignment(CefRefPtr<CefBrowserHost> host,
                               UploadFileRequest request,
                               IpcReplyCallback reply) {
  CefRefPtr<UploadFileDevToolsObserver> observer =
      new UploadFileDevToolsObserver(std::move(host), std::move(request),
                                     std::move(reply));
  observer->Start();
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(
          [](CefRefPtr<UploadFileDevToolsObserver> observer) {
            if (observer) {
              observer->Timeout();
            }
          },
          observer),
      30000);
}

int NextScreenshotDevToolsMessageId() {
  // Use an explicit positive ID instead of ExecuteDevToolsMethod(0)'s auto-ID
  // so an extremely fast DevTools method result cannot race observer
  // initialization.
  static int next_message_id = 900000000;
  if (next_message_id >= 999000000) {
    next_message_id = 900000000;
  }
  return next_message_id++;
}

bool ParseSyntheticKeySpec(const std::string &spec, CefKeyEvent *event) {
  if (!event || spec.empty()) {
    return false;
  }
  std::string key;
  size_t start = 0;
  while (start <= spec.size()) {
    const size_t plus = spec.find('+', start);
    const std::string part = ToLowerAscii(spec.substr(
        start, plus == std::string::npos ? std::string::npos : plus - start));
    if (part == "ctrl" || part == "control") {
      event->modifiers |= EVENTFLAG_CONTROL_DOWN;
    } else if (part == "shift") {
      event->modifiers |= EVENTFLAG_SHIFT_DOWN;
    } else if (part == "alt" || part == "option") {
      event->modifiers |= EVENTFLAG_ALT_DOWN;
    } else if (part == "cmd" || part == "command" || part == "meta") {
      event->modifiers |= EVENTFLAG_COMMAND_DOWN;
    } else if (!part.empty()) {
      if (!key.empty()) {
        return false;
      }
      key = part;
    }
    if (plus == std::string::npos) {
      break;
    }
    start = plus + 1;
  }
  int key_code = 0;
  char16_t character = 0;
  char16_t unmodified = 0;
  if (key == "escape" || key == "esc") {
    key_code = 0x1b;
  } else if (key == "space") {
    key_code = 0x20;
    character = unmodified = u' ';
  } else if (key == "tab") {
    key_code = 0x09;
    character = unmodified = u'\t';
  } else if (key == "enter" || key == "return") {
    key_code = 0x0d;
    character = unmodified = u'\r';
  } else if (key == "backspace") {
    key_code = 0x08;
    character = unmodified = u'\b';
  } else if (key == "delete") {
    key_code = 0x2e;
  } else if (key == "left") {
    key_code = 0x25;
  } else if (key == "up") {
    key_code = 0x26;
  } else if (key == "right") {
    key_code = 0x27;
  } else if (key == "down") {
    key_code = 0x28;
  } else if (key == "home") {
    key_code = 0x24;
  } else if (key == "end") {
    key_code = 0x23;
  } else if (key.size() == 1) {
    const unsigned char raw = static_cast<unsigned char>(key[0]);
    if (raw < 0x20 || raw > 0x7e) {
      return false;
    }
    unmodified = static_cast<char16_t>(raw);
    character = static_cast<char16_t>(
        (event->modifiers & EVENTFLAG_SHIFT_DOWN) ? std::toupper(raw) : raw);
    key_code = std::toupper(raw);
  } else {
    return false;
  }
  event->type = KEYEVENT_RAWKEYDOWN;
  event->windows_key_code = key_code;
  event->native_key_code =
      NativeKeyCodeForSyntheticKey(key_code, unmodified);
  event->character = character;
  event->unmodified_character = unmodified;
  return true;
}

class IpcStringVisitor final : public CefStringVisitor {
public:
  explicit IpcStringVisitor(IpcReplyCallback reply)
      : reply_(std::move(reply)) {}

  void Visit(const CefString &string) override {
    if (reply_) {
      reply_(string.ToString());
      reply_ = nullptr;
    }
  }

private:
  IpcReplyCallback reply_;

  IMPLEMENT_REFCOUNTING(IpcStringVisitor);
  DISALLOW_COPY_AND_ASSIGN(IpcStringVisitor);
};

class CookieListVisitor final : public CefCookieVisitor {
public:
  explicit CookieListVisitor(IpcReplyCallback reply)
      : reply_(std::move(reply)) {}

  bool Visit(const CefCookie &cookie, int count, int total,
             bool &deleteCookie) override {
    deleteCookie = false;
    cookies_.push_back(CookieJson(cookie));
    if (total <= 0 || count + 1 >= total) {
      Finish();
      return false;
    }
    return true;
  }

  void Finish() {
    if (!reply_) {
      return;
    }
    std::ostringstream out;
    out << "{\"cookies\":[";
    for (size_t i = 0; i < cookies_.size(); ++i) {
      if (i) {
        out << ",";
      }
      out << cookies_[i];
    }
    out << "]}";
    auto reply = std::move(reply_);
    reply_ = nullptr;
    reply(out.str());
  }

private:
  IpcReplyCallback reply_;
  std::vector<std::string> cookies_;

  IMPLEMENT_REFCOUNTING(CookieListVisitor);
  DISALLOW_COPY_AND_ASSIGN(CookieListVisitor);
};

class CookieDeleteCallback final : public CefDeleteCookiesCallback {
public:
  explicit CookieDeleteCallback(IpcReplyCallback reply)
      : reply_(std::move(reply)) {}

  void OnComplete(int num_deleted) override {
    if (reply_) {
      reply_("{\"deleted\":" + std::to_string(num_deleted) + "}");
      reply_ = nullptr;
    }
  }

private:
  IpcReplyCallback reply_;

  IMPLEMENT_REFCOUNTING(CookieDeleteCallback);
  DISALLOW_COPY_AND_ASSIGN(CookieDeleteCallback);
};

class CookieSetCallback final : public CefSetCookieCallback {
public:
  explicit CookieSetCallback(IpcReplyCallback reply)
      : reply_(std::move(reply)) {}

  void OnComplete(bool success) override {
    if (reply_) {
      reply_(std::string("{\"success\":") + (success ? "true" : "false") + "}");
      reply_ = nullptr;
    }
  }

private:
  IpcReplyCallback reply_;

  IMPLEMENT_REFCOUNTING(CookieSetCallback);
  DISALLOW_COPY_AND_ASSIGN(CookieSetCallback);
};

void VisitCookiesForUrl(CefRefPtr<CefCookieManager> manager,
                        const std::string &url, IpcReplyCallback reply) {
  if (url.empty()) {
    reply("ERR cookie URL is empty\n");
    return;
  }
  if (!manager) {
    reply("ERR no cookie manager\n");
    return;
  }
  CefRefPtr<CookieListVisitor> visitor(new CookieListVisitor(std::move(reply)));
  if (!manager->VisitUrlCookies(url, true, visitor)) {
    visitor->Finish();
    return;
  }
  CefPostDelayedTask(TID_UI,
                     base::BindOnce(&CookieListVisitor::Finish, visitor), 1500);
}

class URLRequestReplayClient final : public CefURLRequestClient {
public:
  explicit URLRequestReplayClient(IpcReplyCallback reply)
      : reply_(std::move(reply)) {}

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    if (!reply_) {
      return;
    }
    CefRefPtr<CefResponse> response =
        request ? request->GetResponse() : nullptr;
    CefResponse::HeaderMap headers;
    if (response) {
      response->GetHeaderMap(headers);
    }
    std::ostringstream out;
    out << "{"
        << "\"request_status\":"
        << (request ? static_cast<int>(request->GetRequestStatus()) : -1) << ","
        << "\"error\":"
        << (response ? static_cast<int>(response->GetError()) : 0) << ","
        << "\"status\":" << (response ? response->GetStatus() : 0) << ","
        << "\"status_text\":\""
        << JsonEscape(response ? response->GetStatusText().ToString()
                               : std::string())
        << "\","
        << "\"mime_type\":\""
        << JsonEscape(response ? response->GetMimeType().ToString()
                               : std::string())
        << "\","
        << "\"url\":\""
        << JsonEscape(response ? response->GetURL().ToString() : std::string())
        << "\","
        << "\"headers\":" << HeadersJson(headers) << ","
        << "\"body\":\"" << JsonEscape(body_) << "\","
        << "\"body_size\":" << body_.size() << ","
        << "\"body_truncated\":" << (body_truncated_ ? "true" : "false") << "}";
    auto reply = std::move(reply_);
    reply_ = nullptr;
    reply(out.str());
  }

  void OnUploadProgress(CefRefPtr<CefURLRequest> request, int64_t current,
                        int64_t total) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest> request, int64_t current,
                          int64_t total) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest> request, const void *data,
                      size_t data_length) override {
    const size_t remaining =
        body_.size() < (1024 * 1024) ? (1024 * 1024) - body_.size() : 0;
    const size_t take = std::min(data_length, remaining);
    if (take > 0) {
      body_.append(static_cast<const char *>(data), take);
    }
    if (take < data_length) {
      body_truncated_ = true;
    }
  }
  bool GetAuthCredentials(bool isProxy, const CefString &host, int port,
                          const CefString &realm, const CefString &scheme,
                          CefRefPtr<CefAuthCallback> callback) override {
    return false;
  }

private:
  IpcReplyCallback reply_;
  std::string body_;
  bool body_truncated_ = false;

  IMPLEMENT_REFCOUNTING(URLRequestReplayClient);
  DISALLOW_COPY_AND_ASSIGN(URLRequestReplayClient);
};

class ScreenshotDevToolsObserver final : public CefDevToolsMessageObserver {
public:
  ScreenshotDevToolsObserver(uint64_t tab_id, std::string url,
                             IpcReplyCallback reply,
                             std::function<void()> cleanup = {})
      : tab_id_(tab_id), url_(std::move(url)), reply_(std::move(reply)),
        cleanup_(std::move(cleanup)) {}

  void SetRegistration(CefRefPtr<CefRegistration> registration) {
    registration_ = registration;
  }

  void SetMessageId(int message_id) { message_id_ = message_id; }

  void Fail(std::string error) { Finish(std::move(error)); }

  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int message_id,
                              bool success, const void *result,
                              size_t result_size) override {
    if (completed_ || message_id_ == 0 || message_id != message_id_) {
      return;
    }

    if (!success) {
      Finish("ERR screenshot failed: " +
             DevToolsErrorMessage(result, result_size) + "\n");
      return;
    }

    CefRefPtr<CefValue> value =
        CefParseJSON(result, result_size, JSON_PARSER_RFC);
    if (!value || value->GetType() != VTYPE_DICTIONARY) {
      Finish("ERR screenshot failed: invalid devtools response\n");
      return;
    }
    CefRefPtr<CefDictionaryValue> dict = value->GetDictionary();
    if (!dict || !dict->HasKey("data") ||
        dict->GetType("data") != VTYPE_STRING) {
      Finish("ERR screenshot failed: devtools response did not include image "
             "data\n");
      return;
    }

    const std::string data = dict->GetString("data").ToString();
    if (data.empty()) {
      Finish("ERR screenshot failed: empty image data\n");
      return;
    }

    std::ostringstream out;
    out << "{"
        << "\"tabid\":" << tab_id_ << ","
        << "\"url\":\"" << JsonEscape(url_) << "\","
        << "\"mime_type\":\"image/png\","
        << "\"encoding\":\"base64\","
        << "\"data\":\"" << JsonEscape(data) << "\""
        << "}";
    Finish(out.str());
  }

private:
  std::string DevToolsErrorMessage(const void *result,
                                   size_t result_size) const {
    if (!result || result_size == 0) {
      return "unknown error";
    }
    CefRefPtr<CefValue> value =
        CefParseJSON(result, result_size, JSON_PARSER_RFC);
    if (value && value->GetType() == VTYPE_DICTIONARY) {
      CefRefPtr<CefDictionaryValue> dict = value->GetDictionary();
      if (dict && dict->HasKey("message") &&
          dict->GetType("message") == VTYPE_STRING) {
        return dict->GetString("message").ToString();
      }
    }
    return std::string(static_cast<const char *>(result), result_size);
  }

  void Finish(std::string response) {
    if (completed_) {
      return;
    }
    completed_ = true;
    registration_ = nullptr;
    if (cleanup_) {
      auto cleanup = std::move(cleanup_);
      cleanup();
    }
    if (reply_) {
      auto reply = std::move(reply_);
      reply(std::move(response));
    }
  }

  uint64_t tab_id_ = 0;
  std::string url_;
  IpcReplyCallback reply_;
  std::function<void()> cleanup_;
  CefRefPtr<CefRegistration> registration_;
  int message_id_ = 0;
  bool completed_ = false;

  IMPLEMENT_REFCOUNTING(ScreenshotDevToolsObserver);
  DISALLOW_COPY_AND_ASSIGN(ScreenshotDevToolsObserver);
};

void StartScreenshotDevToolsCapture(
    CefRefPtr<CefBrowserHost> host, CefRefPtr<CefDictionaryValue> params,
    CefRefPtr<ScreenshotDevToolsObserver> observer) {
  if (!host || !observer) {
    if (observer) {
      observer->Fail("ERR screenshot failed to start\n");
    }
    return;
  }

  observer->SetRegistration(host->AddDevToolsMessageObserver(observer));
  const int requested_message_id = NextScreenshotDevToolsMessageId();
  observer->SetMessageId(requested_message_id);
  const int message_id = host->ExecuteDevToolsMethod(
      requested_message_id, "Page.captureScreenshot", params);
  if (message_id == 0) {
    observer->Fail("ERR screenshot failed to start\n");
    return;
  }
  if (message_id != requested_message_id) {
    observer->SetMessageId(message_id);
  }
  CefPostDelayedTask(TID_UI,
                     base::BindOnce(
                         [](CefRefPtr<ScreenshotDevToolsObserver> observer) {
                           if (observer) {
                             observer->Fail("ERR screenshot timed out\n");
                           }
                         },
                         observer),
                     30000);
}

class FileChooserActivationCallback final
    : public CefVimbrowserElementActivationCallback {
 public:
  using Completion = base::OnceCallback<void(int, int)>;

  explicit FileChooserActivationCallback(Completion completion)
      : completion_(std::move(completion)) {}

  void OnComplete(int result, int match_count) override {
    if (completion_) {
      std::move(completion_).Run(result, match_count);
    }
  }

 private:
  Completion completion_;

  IMPLEMENT_REFCOUNTING(FileChooserActivationCallback);
  DISALLOW_COPY_AND_ASSIGN(FileChooserActivationCallback);
};

struct FileChooserActivationContext {
  CefRefPtr<BrowserWindow> owner;
  uint64_t generation = 0;
};

struct InspectControlsContext {
  uint64_t tab_id = 0;
  std::string frame_id;
  std::string frame_url;
  IpcReplyCallback reply;
};

struct ActivateControlContext {
  uint64_t tab_id = 0;
  IpcReplyCallback reply;
};

}  // namespace

std::string BrowserWindow::ArmFileChooserUpload(
    uint64_t tab_id,
    std::vector<std::string> paths) {
  if (file_chooser_upload_.phase == FileChooserUploadPhase::kArmed ||
      file_chooser_upload_.phase == FileChooserUploadPhase::kValidating) {
    return UploadFileErrorJson(
        "chooser_already_armed",
        "a file chooser upload is already armed; cancel it explicitly first");
  }
  if (!FindTabIndexById(tab_id)) {
    return UploadFileErrorJson("no_such_tabid", "no tab has the requested id");
  }

  ++file_chooser_upload_.generation;
  file_chooser_upload_.phase = FileChooserUploadPhase::kArmed;
  file_chooser_upload_.tab_id = tab_id;
  file_chooser_upload_.file_count = paths.size();
  file_chooser_upload_.paths = std::move(paths);
  file_chooser_upload_.expires_at =
      std::chrono::steady_clock::now() + std::chrono::seconds(60);
  file_chooser_upload_.error_code.clear();
  file_chooser_upload_.error_message.clear();
  file_chooser_upload_.error_file_index = -1;
  file_chooser_upload_.dialog_mode = FILE_DIALOG_NUM_VALUES;
  file_chooser_upload_.automatic_activation = false;
  file_chooser_upload_.activation_kind.clear();
  file_chooser_upload_.activation_selector.clear();
  file_chooser_upload_.activation_match_count = 0;
  file_chooser_upload_.activation_nonce_high = 0;
  file_chooser_upload_.activation_nonce_low = 0;
  file_chooser_upload_.chooser_callback = nullptr;
  file_chooser_upload_.completion_reply = nullptr;

  CefRefPtr<BrowserWindow> self = this;
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(&BrowserWindow::ExpireFileChooserUpload, self,
                     file_chooser_upload_.generation),
      60000);
  return FileChooserUploadStatusJson(tab_id);
}

void BrowserWindow::StartFileChooserActivationUpload(
    uint64_t tab_id,
    std::string selector,
    std::vector<std::string> paths,
    IpcReplyCallback reply) {
  if (file_chooser_upload_.phase == FileChooserUploadPhase::kArmed ||
      file_chooser_upload_.phase == FileChooserUploadPhase::kValidating) {
    reply(UploadFileErrorJson(
        "chooser_already_armed",
        "a file chooser upload is already armed; cancel it explicitly first"));
    return;
  }

  std::string browser_error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &browser_error);
  if (!browser || !browser->GetHost()) {
    reply(UploadFileErrorJson("tab_unavailable",
                              "tab has no live browser backend"));
    return;
  }

  ++file_chooser_upload_.generation;
  file_chooser_upload_.phase = FileChooserUploadPhase::kArmed;
  file_chooser_upload_.tab_id = tab_id;
  file_chooser_upload_.file_count = paths.size();
  file_chooser_upload_.paths = std::move(paths);
  file_chooser_upload_.expires_at =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(kAutomaticUploadChooserTimeoutMs);
  file_chooser_upload_.error_code.clear();
  file_chooser_upload_.error_message.clear();
  file_chooser_upload_.error_file_index = -1;
  file_chooser_upload_.dialog_mode = FILE_DIALOG_NUM_VALUES;
  file_chooser_upload_.automatic_activation = true;
  file_chooser_upload_.activation_kind = "activate";
  file_chooser_upload_.activation_selector = std::move(selector);
  // A successful exact-target activation necessarily has one match. The backend
  // callback will replace this value for structured zero/ambiguous failures.
  file_chooser_upload_.activation_match_count = 1;
  file_chooser_upload_.activation_nonce_high = 0;
  file_chooser_upload_.activation_nonce_low = 0;
  file_chooser_upload_.chooser_callback = nullptr;
  file_chooser_upload_.completion_reply = std::move(reply);

  const uint64_t generation = file_chooser_upload_.generation;
  CefRefPtr<FileChooserActivationCallback> activation_callback =
      new FileChooserActivationCallback(base::BindOnce(
          [](CefRefPtr<BrowserWindow> owner, uint64_t generation, int result,
             int match_count) {
            if (owner) {
              owner->FinishFileChooserElementActivation(generation, result,
                                                        match_count);
            }
          },
          CefRefPtr<BrowserWindow>(this), generation));
  const bool started = CefBrowserHost::VimbrowserActivateElementBySelector(
      browser->GetIdentifier(), file_chooser_upload_.activation_selector,
      file_chooser_upload_.activation_nonce_high,
      file_chooser_upload_.activation_nonce_low, activation_callback);
  if (!started) {
    file_chooser_upload_.phase = FileChooserUploadPhase::kFailed;
    file_chooser_upload_.paths.clear();
    file_chooser_upload_.error_code = "activation_backend_unavailable";
    file_chooser_upload_.error_message =
        "custom Chromium element activation backend is unavailable";
    ReplyToAutomaticFileChooserUpload(false);
    return;
  }

  CefRefPtr<BrowserWindow> self = this;
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(&BrowserWindow::ExpireFileChooserUpload, self,
                     file_chooser_upload_.generation),
      kAutomaticUploadChooserTimeoutMs);
}

void BrowserWindow::StartFileChooserHandleUpload(
    uint64_t tab_id,
    std::string handle,
    std::vector<std::string> paths,
    IpcReplyCallback reply) {
  if (file_chooser_upload_.phase == FileChooserUploadPhase::kArmed ||
      file_chooser_upload_.phase == FileChooserUploadPhase::kValidating) {
    reply(UploadFileErrorJson(
        "chooser_already_armed",
        "a file chooser upload is already armed; cancel it explicitly first"));
    return;
  }
  std::string browser_error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &browser_error);
  if (!browser || !browser->GetHost()) {
    reply(UploadFileErrorJson("tab_unavailable",
                              "tab has no live browser backend"));
    return;
  }

  ++file_chooser_upload_.generation;
  file_chooser_upload_.phase = FileChooserUploadPhase::kArmed;
  file_chooser_upload_.tab_id = tab_id;
  file_chooser_upload_.file_count = paths.size();
  file_chooser_upload_.paths = std::move(paths);
  file_chooser_upload_.expires_at =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(kAutomaticUploadChooserTimeoutMs);
  file_chooser_upload_.error_code.clear();
  file_chooser_upload_.error_message.clear();
  file_chooser_upload_.error_file_index = -1;
  file_chooser_upload_.dialog_mode = FILE_DIALOG_NUM_VALUES;
  file_chooser_upload_.automatic_activation = true;
  file_chooser_upload_.activation_kind = "handle";
  file_chooser_upload_.activation_selector.clear();
  file_chooser_upload_.activation_match_count = 1;
  file_chooser_upload_.activation_nonce_high = 0;
  file_chooser_upload_.activation_nonce_low = 0;
  file_chooser_upload_.chooser_callback = nullptr;
  file_chooser_upload_.completion_reply = std::move(reply);

  auto context = std::make_unique<FileChooserActivationContext>();
  context->owner = this;
  context->generation = file_chooser_upload_.generation;
  auto* context_ptr = context.release();
  const bool started = vimbrowser_activate_element_handle(
      browser->GetIdentifier(), handle.data(), handle.size(),
      false,
      &file_chooser_upload_.activation_nonce_high,
      &file_chooser_upload_.activation_nonce_low,
      +[](void* user_data, int result, int match_count) {
        std::unique_ptr<FileChooserActivationContext> context(
            static_cast<FileChooserActivationContext*>(user_data));
        if (context && context->owner) {
          context->owner->FinishFileChooserElementActivation(
              context->generation, result, match_count);
        }
      },
      context_ptr);
  if (!started) {
    delete context_ptr;
    file_chooser_upload_.phase = FileChooserUploadPhase::kFailed;
    file_chooser_upload_.paths.clear();
    file_chooser_upload_.error_code = "activation_backend_unavailable";
    file_chooser_upload_.error_message =
        "custom Chromium element-handle backend is unavailable";
    ReplyToAutomaticFileChooserUpload(false);
    return;
  }

  CefRefPtr<BrowserWindow> self = this;
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(&BrowserWindow::ExpireFileChooserUpload, self,
                     file_chooser_upload_.generation),
      kAutomaticUploadChooserTimeoutMs);
}

std::string BrowserWindow::FileChooserUploadStatusJson(uint64_t tab_id) const {
  FileChooserUploadPhase phase = FileChooserUploadPhase::kNone;
  if (file_chooser_upload_.tab_id == tab_id) {
    phase = file_chooser_upload_.phase;
  }
  const char* state = "none";
  switch (phase) {
    case FileChooserUploadPhase::kNone: state = "none"; break;
    case FileChooserUploadPhase::kArmed: state = "armed"; break;
    case FileChooserUploadPhase::kValidating: state = "validating"; break;
    case FileChooserUploadPhase::kConsumed: state = "consumed"; break;
    case FileChooserUploadPhase::kFailed: state = "failed"; break;
    case FileChooserUploadPhase::kExpired: state = "expired"; break;
    case FileChooserUploadPhase::kCanceled: state = "canceled"; break;
  }

  long long expires_in_ms = 0;
  if (phase == FileChooserUploadPhase::kArmed) {
    expires_in_ms = std::max<long long>(
        0, std::chrono::duration_cast<std::chrono::milliseconds>(
               file_chooser_upload_.expires_at -
               std::chrono::steady_clock::now())
               .count());
  }
  const char* dialog_mode = "none";
  if (file_chooser_upload_.tab_id == tab_id) {
    switch (file_chooser_upload_.dialog_mode) {
      case FILE_DIALOG_OPEN: dialog_mode = "open"; break;
      case FILE_DIALOG_OPEN_MULTIPLE: dialog_mode = "open_multiple"; break;
      case FILE_DIALOG_OPEN_FOLDER: dialog_mode = "open_folder"; break;
      case FILE_DIALOG_SAVE: dialog_mode = "save"; break;
      case FILE_DIALOG_NUM_VALUES: dialog_mode = "none"; break;
    }
  }

  std::ostringstream out;
  out << "{\"ok\":true,\"tabid\":" << tab_id
      << ",\"target\":{\"kind\":\"chooser\"},\"chooser\":{\"state\":\""
      << state << "\",\"file_count\":"
      << (file_chooser_upload_.tab_id == tab_id
              ? file_chooser_upload_.file_count
              : 0)
      << ",\"expires_in_ms\":" << expires_in_ms
      << ",\"dialog_mode\":\"" << dialog_mode << "\"";
  if (file_chooser_upload_.tab_id == tab_id &&
      !file_chooser_upload_.error_code.empty()) {
    out << ",\"error\":{\"code\":\""
        << JsonEscape(file_chooser_upload_.error_code)
        << "\",\"message\":\""
        << JsonEscape(file_chooser_upload_.error_message) << "\"";
    if (file_chooser_upload_.error_file_index >= 0) {
      out << ",\"file_index\":"
          << file_chooser_upload_.error_file_index;
    }
    out << "}";
  }
  out << "}}";
  return out.str();
}

std::string BrowserWindow::CancelFileChooserUpload(uint64_t tab_id) {
  if (file_chooser_upload_.tab_id == tab_id &&
      (file_chooser_upload_.phase == FileChooserUploadPhase::kArmed ||
       file_chooser_upload_.phase == FileChooserUploadPhase::kValidating)) {
    ++file_chooser_upload_.generation;
    file_chooser_upload_.phase = FileChooserUploadPhase::kCanceled;
    file_chooser_upload_.paths.clear();
    file_chooser_upload_.error_code = "canceled";
    file_chooser_upload_.error_message =
        "file chooser upload was canceled explicitly";
    file_chooser_upload_.error_file_index = -1;
    CefRefPtr<CefFileDialogCallback> chooser_callback =
        std::move(file_chooser_upload_.chooser_callback);
    if (chooser_callback) {
      chooser_callback->Cancel();
    }
    ReplyToAutomaticFileChooserUpload(false);
  }
  return FileChooserUploadStatusJson(tab_id);
}

void BrowserWindow::FinishFileChooserElementActivation(uint64_t generation,
                                                       int result,
                                                       int match_count) {
  if (file_chooser_upload_.generation != generation ||
      !file_chooser_upload_.automatic_activation ||
      (file_chooser_upload_.phase != FileChooserUploadPhase::kArmed &&
       file_chooser_upload_.phase != FileChooserUploadPhase::kValidating)) {
    return;
  }
  file_chooser_upload_.activation_match_count = match_count;
  const auto activation_result =
      static_cast<VimbrowserElementActivationResult>(result);
  if (activation_result == VimbrowserElementActivationResult::kDispatched) {
    return;
  }

  ++file_chooser_upload_.generation;
  file_chooser_upload_.phase = FileChooserUploadPhase::kFailed;
  file_chooser_upload_.paths.clear();
  const VimbrowserElementActivationError error =
      ElementActivationErrorForResult(activation_result);
  file_chooser_upload_.error_code = error.code;
  file_chooser_upload_.error_message = error.message;
  CefRefPtr<CefFileDialogCallback> chooser_callback =
      std::move(file_chooser_upload_.chooser_callback);
  if (chooser_callback) {
    chooser_callback->Cancel();
  }
  ReplyToAutomaticFileChooserUpload(false);
}

void BrowserWindow::ReplyToAutomaticFileChooserUpload(bool success) {
  if (!file_chooser_upload_.automatic_activation ||
      !file_chooser_upload_.completion_reply) {
    return;
  }

  IpcReplyCallback reply = std::move(file_chooser_upload_.completion_reply);
  if (!success) {
    reply(UploadFileErrorJson(
        file_chooser_upload_.error_code.empty()
            ? std::string_view("upload_failed")
            : std::string_view(file_chooser_upload_.error_code),
        file_chooser_upload_.error_message.empty()
            ? std::string_view("native chooser upload failed")
            : std::string_view(file_chooser_upload_.error_message),
        file_chooser_upload_.error_file_index,
        file_chooser_upload_.error_code == "ambiguous_target"
            ? file_chooser_upload_.activation_match_count
            : -1));
    return;
  }

  const char* dialog_mode = "none";
  switch (file_chooser_upload_.dialog_mode) {
    case FILE_DIALOG_OPEN: dialog_mode = "open"; break;
    case FILE_DIALOG_OPEN_MULTIPLE: dialog_mode = "open_multiple"; break;
    case FILE_DIALOG_OPEN_FOLDER: dialog_mode = "open_folder"; break;
    case FILE_DIALOG_SAVE: dialog_mode = "save"; break;
    case FILE_DIALOG_NUM_VALUES: dialog_mode = "none"; break;
  }
  std::ostringstream out;
  out << "{\"ok\":true,\"tabid\":" << file_chooser_upload_.tab_id
      << ",\"file_count\":" << file_chooser_upload_.file_count
      << ",\"target\":{\"kind\":\""
      << (file_chooser_upload_.activation_kind == "handle" ? "handle"
                                                            : "activate")
      << "\",\"match_count\":"
      << file_chooser_upload_.activation_match_count
      << "},\"chooser\":{\"state\":\"consumed\",\"dialog_mode\":\""
      << dialog_mode << "\"}}";
  reply(out.str());
}

void BrowserWindow::ExpireFileChooserUpload(uint64_t generation) {
  const bool automatic_pending =
      file_chooser_upload_.automatic_activation &&
      (file_chooser_upload_.phase == FileChooserUploadPhase::kArmed ||
       file_chooser_upload_.phase == FileChooserUploadPhase::kValidating);
  if (file_chooser_upload_.generation != generation ||
      (!automatic_pending &&
       file_chooser_upload_.phase != FileChooserUploadPhase::kArmed)) {
    return;
  }
  ++file_chooser_upload_.generation;
  file_chooser_upload_.phase = FileChooserUploadPhase::kExpired;
  file_chooser_upload_.paths.clear();
  file_chooser_upload_.error_code = "expired";
  file_chooser_upload_.error_message =
      file_chooser_upload_.automatic_activation
          ? "activated element did not open a file chooser before the deadline"
          : "no file chooser request arrived before the 60 second deadline";
  if (file_chooser_upload_.automatic_activation) {
    file_chooser_upload_.error_code = "chooser_not_opened";
  }
  file_chooser_upload_.error_file_index = -1;
  CefRefPtr<CefFileDialogCallback> chooser_callback =
      std::move(file_chooser_upload_.chooser_callback);
  if (chooser_callback) {
    chooser_callback->Cancel();
  }
  ReplyToAutomaticFileChooserUpload(false);
}

void BrowserWindow::CancelFileChooserUploadForClient(BrowserClient* client,
                                                      std::string code,
                                                      std::string message) {
  if (!client ||
      (file_chooser_upload_.phase != FileChooserUploadPhase::kArmed &&
       file_chooser_upload_.phase != FileChooserUploadPhase::kValidating)) {
    return;
  }
  const std::optional<size_t> index =
      FindTabIndexById(file_chooser_upload_.tab_id);
  if (!index || tabs_[*index].client.get() != client) {
    return;
  }
  ++file_chooser_upload_.generation;
  file_chooser_upload_.phase = FileChooserUploadPhase::kCanceled;
  file_chooser_upload_.paths.clear();
  file_chooser_upload_.error_code = std::move(code);
  file_chooser_upload_.error_message = std::move(message);
  file_chooser_upload_.error_file_index = -1;
  CefRefPtr<CefFileDialogCallback> chooser_callback =
      std::move(file_chooser_upload_.chooser_callback);
  if (chooser_callback) {
    chooser_callback->Cancel();
  }
  ReplyToAutomaticFileChooserUpload(false);
}

bool BrowserWindow::OnClientFileDialog(
    BrowserClient* client,
    CefRefPtr<CefBrowser> browser,
    cef_file_dialog_mode_t mode,
    const std::vector<CefString>& accept_filters,
    const std::vector<CefString>& accept_extensions,
    bool has_activation_nonce,
    uint64_t activation_nonce_high,
    uint64_t activation_nonce_low,
    CefRefPtr<CefFileDialogCallback> callback) {
  const bool automatic_in_progress =
      file_chooser_upload_.automatic_activation &&
      (file_chooser_upload_.phase == FileChooserUploadPhase::kArmed ||
       file_chooser_upload_.phase == FileChooserUploadPhase::kValidating);
  if (!client || !browser || !callback ||
      (file_chooser_upload_.phase != FileChooserUploadPhase::kArmed &&
       !automatic_in_progress)) {
    return false;
  }
  const std::optional<size_t> index =
      FindTabIndexById(file_chooser_upload_.tab_id);
  if (!index || tabs_[*index].client.get() != client ||
      !tabs_[*index].client->browser() ||
      tabs_[*index].client->browser()->GetIdentifier() !=
          browser->GetIdentifier()) {
      return false;
  }

  if (file_chooser_upload_.automatic_activation) {
    const bool matching_nonce =
        has_activation_nonce &&
        file_chooser_upload_.activation_nonce_high == activation_nonce_high &&
        file_chooser_upload_.activation_nonce_low == activation_nonce_low;
    if (!matching_nonce) {
      // Do not let another chooser race the atomic operation or appear as a
      // native modal. It cannot consume the armed paths without the nonce.
      callback->Cancel();
      return true;
    }
    if (std::chrono::steady_clock::now() >=
        file_chooser_upload_.expires_at) {
      ++file_chooser_upload_.generation;
      file_chooser_upload_.phase = FileChooserUploadPhase::kExpired;
      file_chooser_upload_.paths.clear();
      file_chooser_upload_.error_code = "chooser_not_opened";
      file_chooser_upload_.error_message =
          "activated element did not open a file chooser before the deadline";
      file_chooser_upload_.error_file_index = -1;
      CefRefPtr<CefFileDialogCallback> chooser_callback =
          std::move(file_chooser_upload_.chooser_callback);
      if (chooser_callback) {
        chooser_callback->Cancel();
      }
      callback->Cancel();
      ReplyToAutomaticFileChooserUpload(false);
      return true;
    }
    if (file_chooser_upload_.phase == FileChooserUploadPhase::kValidating) {
      // Only one causally tagged chooser can be supplied. Cancel any additional
      // request emitted by the same activation while the first is validating.
      callback->Cancel();
      return true;
    }
  } else if (has_activation_nonce) {
    // Tagged requests are private automatic operations. The CEF backend will
    // cancel this request if no matching automatic arm claims it.
    return false;
  }

  file_chooser_upload_.dialog_mode = mode;
  if (mode != FILE_DIALOG_OPEN && mode != FILE_DIALOG_OPEN_MULTIPLE) {
    const bool automatic_activation =
        file_chooser_upload_.automatic_activation;
    file_chooser_upload_.phase = FileChooserUploadPhase::kFailed;
    file_chooser_upload_.paths.clear();
    file_chooser_upload_.error_code = "wrong_dialog_mode";
    file_chooser_upload_.error_message =
        "armed upload only accepts open-file chooser requests";
    file_chooser_upload_.error_file_index = -1;
    if (automatic_activation) {
      callback->Cancel();
      ReplyToAutomaticFileChooserUpload(false);
      return true;
    }
    return false;  // Preserve manual folder/save dialog behavior.
  }
  if (mode == FILE_DIALOG_OPEN && file_chooser_upload_.paths.size() > 1) {
    file_chooser_upload_.phase = FileChooserUploadPhase::kFailed;
    file_chooser_upload_.paths.clear();
    file_chooser_upload_.error_code = "multiple_not_allowed";
    file_chooser_upload_.error_message =
        "file chooser request only accepts one file";
    file_chooser_upload_.error_file_index = -1;
    callback->Cancel();
    ReplyToAutomaticFileChooserUpload(false);
    return true;
  }

  const std::string accept =
      FileChooserAcceptConstraint(accept_filters, accept_extensions);
  for (size_t i = 0; i < file_chooser_upload_.paths.size(); ++i) {
    if (!UploadFileMatchesAccept(file_chooser_upload_.paths[i], accept)) {
      file_chooser_upload_.phase = FileChooserUploadPhase::kFailed;
      file_chooser_upload_.paths.clear();
      file_chooser_upload_.error_code = "accept_mismatch";
      file_chooser_upload_.error_message =
          "upload file does not match the chooser accept constraint";
      file_chooser_upload_.error_file_index = static_cast<int>(i);
      callback->Cancel();
      ReplyToAutomaticFileChooserUpload(false);
      return true;
    }
  }

  file_chooser_upload_.phase = FileChooserUploadPhase::kValidating;
  file_chooser_upload_.chooser_callback = callback;
  const uint64_t generation = file_chooser_upload_.generation;
  const std::vector<std::string> paths = file_chooser_upload_.paths;
  CefRefPtr<BrowserWindow> self = this;
  if (!CefPostTask(
          TID_FILE_USER_BLOCKING,
          base::BindOnce(
              [](CefRefPtr<BrowserWindow> self, uint64_t generation,
                 std::vector<std::string> paths,
                 CefRefPtr<CefFileDialogCallback> callback) {
                UploadFileValidation validation =
                    ValidateUploadFilePaths(paths);
                if (!CefPostTask(
                        TID_UI,
                        base::BindOnce(
                            &BrowserWindow::FinishFileChooserUploadValidation,
                            self, generation, validation.ok,
                            std::move(validation.error_code),
                            std::move(validation.error_message),
                            validation.error_file_index,
                            std::move(validation.canonical_paths), callback))) {
                  callback->Cancel();
                }
              },
              self, generation, paths, callback))) {
    file_chooser_upload_.phase = FileChooserUploadPhase::kFailed;
    file_chooser_upload_.paths.clear();
    file_chooser_upload_.error_code = "internal_error";
    file_chooser_upload_.error_message =
        "failed to start chooser-time file validation";
    file_chooser_upload_.error_file_index = -1;
    file_chooser_upload_.chooser_callback = nullptr;
    callback->Cancel();
    ReplyToAutomaticFileChooserUpload(false);
  }
  return true;
}

void BrowserWindow::FinishFileChooserUploadValidation(
    uint64_t generation,
    bool valid,
    std::string error_code,
    std::string error_message,
    int error_file_index,
    std::vector<std::string> canonical_paths,
    CefRefPtr<CefFileDialogCallback> callback) {
  if (!callback) {
    return;
  }
  if (file_chooser_upload_.generation != generation ||
      file_chooser_upload_.phase != FileChooserUploadPhase::kValidating) {
    callback->Cancel();
    return;
  }
  if (!valid) {
    file_chooser_upload_.phase = FileChooserUploadPhase::kFailed;
    file_chooser_upload_.paths.clear();
    file_chooser_upload_.error_code = std::move(error_code);
    file_chooser_upload_.error_message = std::move(error_message);
    file_chooser_upload_.error_file_index = error_file_index;
    file_chooser_upload_.chooser_callback = nullptr;
    callback->Cancel();
    ReplyToAutomaticFileChooserUpload(false);
    return;
  }

  std::vector<CefString> files;
  files.reserve(canonical_paths.size());
  for (const std::string& path : canonical_paths) {
    files.emplace_back(path);
  }
  file_chooser_upload_.phase = FileChooserUploadPhase::kConsumed;
  file_chooser_upload_.paths.clear();
  file_chooser_upload_.error_code.clear();
  file_chooser_upload_.error_message.clear();
  file_chooser_upload_.error_file_index = -1;
  file_chooser_upload_.chooser_callback = nullptr;
  callback->Continue(files);
  ReplyToAutomaticFileChooserUpload(true);
}

void BrowserWindow::CompleteJsIpcRequest(uint64_t request_id,
                                         std::string response) {
  auto it = pending_js_ipc_.find(request_id);
  if (it == pending_js_ipc_.end()) {
    return;
  }
  IpcReplyCallback reply = std::move(it->second);
  pending_js_ipc_.erase(it);
  reply(std::move(response));
}

std::string BrowserWindow::FrameTreeJson(uint64_t tab_id) const {
  std::string error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error);
  if (!browser) {
    return UploadFileErrorJson("tab_unavailable",
                               "tab has no live browser backend");
  }
  std::vector<CefString> frame_ids;
  browser->GetFrameIdentifiers(frame_ids);
  CefRefPtr<CefFrame> main = browser->GetMainFrame();
  std::ostringstream out;
  out << "{\"ok\":true,\"tabid\":" << tab_id << ",\"main_frame_id\":\""
      << JsonEscape(main ? main->GetIdentifier().ToString() : std::string())
      << "\",\"frames\":[";
  bool first = true;
  for (const CefString& id : frame_ids) {
    CefRefPtr<CefFrame> frame = browser->GetFrameByIdentifier(id);
    if (!frame || !frame->IsValid()) {
      continue;
    }
    CefRefPtr<CefFrame> parent = frame->GetParent();
    int depth = 0;
    for (CefRefPtr<CefFrame> current = parent; current && depth < 64;
         current = current->GetParent()) {
      ++depth;
    }
    if (!first) {
      out << ',';
    }
    first = false;
    const std::string frame_id = id.ToString();
    out << "{\"id\":\"" << JsonEscape(frame_id)
        << "\",\"parent_id\":";
    if (parent) {
      out << "\"" << JsonEscape(parent->GetIdentifier().ToString()) << "\"";
    } else {
      out << "null";
    }
    out << ",\"name\":\"" << JsonEscape(frame->GetName().ToString())
        << "\",\"url\":\"" << JsonEscape(frame->GetURL().ToString())
        << "\",\"main\":" << (frame->IsMain() ? "true" : "false")
        << ",\"focused\":" << (frame->IsFocused() ? "true" : "false")
        << ",\"depth\":" << depth << ",\"out_of_process\":"
        << (vimbrowser_frame_is_out_of_process(
                browser->GetIdentifier(), frame_id.data(), frame_id.size())
                ? "true"
                : "false")
        << '}';
  }
  out << "]}";
  return out.str();
}

void BrowserWindow::HandleInspectControlsIpcCommand(
    uint64_t tab_id,
    std::string encoded_query,
    IpcReplyCallback reply) {
  InspectControlsRequest request;
  std::string payload_error;
  if (!DecodeInspectControlsPayload(encoded_query, &request, &payload_error)) {
    reply(std::move(payload_error));
    return;
  }
  std::string browser_error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &browser_error);
  CefRefPtr<CefFrame> frame =
      browser ? browser->GetFrameByIdentifier(request.frame_id) : nullptr;
  if (!browser || !frame || !frame->IsValid()) {
    reply(UploadFileErrorJson("stale_frame",
                              "inspection frame is not active in this tab"));
    return;
  }

  auto context = std::make_unique<InspectControlsContext>();
  context->tab_id = tab_id;
  context->frame_id = request.frame_id;
  context->frame_url = frame->GetURL().ToString();
  context->reply = std::move(reply);
  auto* context_ptr = context.release();
  const bool started = vimbrowser_inspect_frame_controls(
      browser->GetIdentifier(), request.frame_id.data(), request.frame_id.size(),
      request.role.data(), request.role.size(), request.exact_name.data(),
      request.exact_name.size(), request.context_contains.data(),
      request.context_contains.size(), request.limit,
      +[](void* user_data, int result, const char* json, size_t json_size) {
        std::unique_ptr<InspectControlsContext> context(
            static_cast<InspectControlsContext*>(user_data));
        if (!context || !context->reply) {
          return;
        }
        if (result != 0) {
          context->reply(UploadFileErrorJson(
              result == 1 ? "stale_document" : "inspection_backend_unavailable",
              result == 1
                  ? "frame document changed or became unavailable during inspection"
                  : "custom Chromium control inspection backend is unavailable"));
          return;
        }
        const std::string inspection =
            json && json_size ? std::string(json, json_size) : "{}";
        std::ostringstream out;
        out << "{\"ok\":true,\"tabid\":" << context->tab_id
            << ",\"frame\":{\"id\":\""
            << JsonEscape(context->frame_id) << "\",\"url\":\""
            << JsonEscape(context->frame_url) << "\"},\"inspection\":"
            << inspection << '}';
        context->reply(out.str());
      },
      context_ptr);
  if (!started) {
    std::unique_ptr<InspectControlsContext> cleanup(context_ptr);
    cleanup->reply(UploadFileErrorJson(
        "inspection_backend_unavailable",
        "custom Chromium control inspection backend is unavailable"));
  }
}

void BrowserWindow::HandleHtmlIpcCommand(uint64_t tab_id,
                                         std::string frame_id,
                                         bool text,
                                         IpcReplyCallback reply) {
  std::string error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error);
  if (!browser) {
    reply(error);
    return;
  }
  CefRefPtr<CefFrame> frame = frame_id.empty()
                                  ? browser->GetMainFrame()
                                  : browser->GetFrameByIdentifier(frame_id);
  if (!frame) {
    reply("ERR tab has no requested frame\n");
    return;
  }
  CefRefPtr<IpcStringVisitor> visitor(new IpcStringVisitor(std::move(reply)));
  if (text) {
    frame->GetText(visitor);
  } else {
    frame->GetSource(visitor);
  }
}

void BrowserWindow::HandleJsIpcCommand(uint64_t tab_id,
                                       std::string frame_id,
                                       std::string code,
                                       IpcReplyCallback reply,
                                       int timeout_ms) {
  std::string error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error);
  if (!browser) {
    reply(error);
    return;
  }
  CefRefPtr<CefFrame> frame = frame_id.empty()
                                  ? browser->GetMainFrame()
                                  : browser->GetFrameByIdentifier(frame_id);
  if (!frame) {
    reply("ERR tab has no requested frame\n");
    return;
  }

  const uint64_t request_id = next_ipc_request_id_++;
  pending_js_ipc_[request_id] = std::move(reply);

  CefRefPtr<CefProcessMessage> message =
      CefProcessMessage::Create(kJsEvalMessage);
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  args->SetString(0, std::to_string(request_id));
  args->SetString(1, code);
  frame->SendProcessMessage(PID_RENDERER, message);

  CefRefPtr<BrowserWindow> self = this;
  CefPostDelayedTask(TID_UI,
                     base::BindOnce(&BrowserWindow::CompleteJsIpcRequest, self,
                                    request_id,
                                    std::string("ERR js command timed out\n")),
                     timeout_ms);
}

void BrowserWindow::ReadJsFileForIpc(uint64_t tab_id, std::string path,
                                     IpcReplyCallback reply) {
  std::string error;
  std::string code = ReadRegularFileToString(path, kMaxJsFileBytes, &error);
  CefRefPtr<BrowserWindow> self = this;
  if (!CefPostTask(TID_UI, base::BindOnce(&BrowserWindow::FinishJsFileForIpc,
                                          self, tab_id, std::move(code),
                                          std::move(error), reply))) {
    reply("ERR failed to post js-file result to UI thread\n");
  }
}

void BrowserWindow::FinishJsFileForIpc(uint64_t tab_id, std::string code,
                                       std::string error,
                                       IpcReplyCallback reply) {
  if (!error.empty()) {
    reply(std::move(error));
    return;
  }
  HandleJsIpcCommand(tab_id, {}, std::move(code), std::move(reply));
}

void BrowserWindow::HandleCookiesIpcCommand(uint64_t tab_id,
                                            std::string url_override,
                                            IpcReplyCallback reply) {
  std::string error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error);
  if (!browser) {
    reply(error);
    return;
  }
  std::string url = std::move(url_override);
  if (url.empty()) {
    url = browser->GetMainFrame() ? browser->GetMainFrame()->GetURL().ToString()
                                  : std::string();
  }
  if (url.empty()) {
    reply("ERR tab has no url\n");
    return;
  }
  CefRefPtr<CefRequestContext> context =
      browser->GetHost() ? browser->GetHost()->GetRequestContext() : nullptr;
  CefRefPtr<CefCookieManager> manager =
      context ? context->GetCookieManager(nullptr) : nullptr;
  VisitCookiesForUrl(manager, url, std::move(reply));
}

void BrowserWindow::HandleCookiesForUrlIpcCommand(std::string url,
                                                  IpcReplyCallback reply) {
  CefRefPtr<CefCookieManager> manager =
      CefCookieManager::GetGlobalManager(nullptr);
  VisitCookiesForUrl(manager, url, std::move(reply));
}

void BrowserWindow::HandleCookieDeleteIpcCommand(uint64_t tab_id,
                                                 std::string name,
                                                 IpcReplyCallback reply) {
  std::string error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error);
  if (!browser) {
    reply(error);
    return;
  }
  const std::string url = browser->GetMainFrame()
                              ? browser->GetMainFrame()->GetURL().ToString()
                              : std::string();
  if (url.empty()) {
    reply("ERR tab has no url\n");
    return;
  }
  CefRefPtr<CefRequestContext> context =
      browser->GetHost() ? browser->GetHost()->GetRequestContext() : nullptr;
  CefRefPtr<CefCookieManager> manager =
      context ? context->GetCookieManager(nullptr) : nullptr;
  if (!manager) {
    reply("ERR no cookie manager\n");
    return;
  }
  if (!manager->DeleteCookies(url, name,
                              new CookieDeleteCallback(std::move(reply)))) {
    reply("ERR cookie delete failed to start\n");
  }
}

void BrowserWindow::HandleCookieSetIpcCommand(uint64_t tab_id, std::string name,
                                              std::string value,
                                              std::string domain,
                                              std::string path,
                                              IpcReplyCallback reply) {
  std::string error;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error);
  if (!browser) {
    reply(error);
    return;
  }
  const std::string url = browser->GetMainFrame()
                              ? browser->GetMainFrame()->GetURL().ToString()
                              : std::string();
  if (url.empty()) {
    reply("ERR tab has no url\n");
    return;
  }
  CefRefPtr<CefRequestContext> context =
      browser->GetHost() ? browser->GetHost()->GetRequestContext() : nullptr;
  CefRefPtr<CefCookieManager> manager =
      context ? context->GetCookieManager(nullptr) : nullptr;
  if (!manager) {
    reply("ERR no cookie manager\n");
    return;
  }
  CefCookie cookie;
  CefString(&cookie.name).FromString(name);
  CefString(&cookie.value).FromString(value);
  CefString(&cookie.domain).FromString(domain);
  CefString(&cookie.path).FromString(path.empty() ? "/" : path);
  cookie.secure = StartsWithCaseInsensitive(url, "https://") ? 1 : 0;
  cookie.httponly = 0;
  cookie.has_expires = 0;
  cookie.same_site = CEF_COOKIE_SAME_SITE_UNSPECIFIED;
  if (!manager->SetCookie(url, cookie,
                          new CookieSetCallback(std::move(reply)))) {
    reply("ERR cookie set failed to start\n");
  }
}

void BrowserWindow::HandleNetworkReplayIpcCommand(uint64_t tab_id,
                                                  uint64_t request_id,
                                                  IpcReplyCallback reply) {
  std::string error;
  size_t index = 0;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error, &index);
  if (!browser) {
    reply(error);
    return;
  }
  if (!tabs_[index].client) {
    reply("ERR tab has no client\n");
    return;
  }
  CefRefPtr<CefRequest> request =
      tabs_[index].client->BuildReplayRequest(request_id, &error);
  if (!request) {
    reply(error);
    return;
  }
  CefRefPtr<URLRequestReplayClient> client(
      new URLRequestReplayClient(std::move(reply)));
  CefRefPtr<CefRequestContext> context =
      browser->GetHost() ? browser->GetHost()->GetRequestContext() : nullptr;
  CefURLRequest::Create(request, client, context);
}

void BrowserWindow::HandleScreenshotIpcCommand(uint64_t tab_id,
                                               IpcReplyCallback reply) {
  std::string error;
  size_t index = 0;
  CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &error, &index);
  if (!browser) {
    reply(error);
    return;
  }
  CefRefPtr<CefBrowserHost> host = browser->GetHost();
  if (!host) {
    reply("ERR tab has no browser host\n");
    return;
  }

  std::string url = tabs_[index].url;
  if (browser->GetMainFrame()) {
    const std::string frame_url = browser->GetMainFrame()->GetURL().ToString();
    if (!frame_url.empty()) {
      url = frame_url;
    }
  }

  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString("format", "png");
  params->SetBool("fromSurface", true);
  params->SetBool("captureBeyondViewport", false);
  params->SetBool("optimizeForSpeed", true);

  if (index == active_index_) {
    CefRefPtr<ScreenshotDevToolsObserver> observer =
        new ScreenshotDevToolsObserver(tab_id, std::move(url),
                                       std::move(reply));
    StartScreenshotDevToolsCapture(host, params, observer);
    return;
  }

  // Paint the inactive tab into a background compositor surface without
  // activating or focusing it. CEF/Views only keeps a reliable surface for
  // views that are attached and visible, so briefly show the target behind the
  // active view, keep the active view frontmost, then let the backend CDP
  // new-surface path copy the target's own surface. The cleanup hides the
  // target again if it is still inactive.
  CefRefPtr<CefBrowserView> target_view = tabs_[index].view;
  CefRefPtr<CefBrowserView> active_view =
      active_index_ < tabs_.size() ? tabs_[active_index_].view : nullptr;
  if (target_view) {
    target_view->SetVisible(true);
  }
  if (content_inner_panel_ && active_view && active_view != target_view) {
    content_inner_panel_->ReorderChildView(active_view, -1);
  }
  if (content_inner_panel_ && content_inner_panel_->GetLayout()) {
    content_inner_panel_->Layout();
  }

  CefRefPtr<BrowserWindow> self(this);
  auto cleanup = [self, target_view, tab_id]() {
    const std::optional<size_t> index = self->FindTabIndexById(tab_id);
    if (!index || *index != self->active_index_) {
      if (target_view) {
        target_view->SetVisible(false);
      }
    }
    if (self->content_inner_panel_ && self->content_inner_panel_->GetLayout()) {
      self->content_inner_panel_->Layout();
    }
  };

  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(
          [](CefRefPtr<CefBrowserHost> host,
             CefRefPtr<CefDictionaryValue> params, uint64_t tab_id,
             std::string url, IpcReplyCallback reply,
             std::function<void()> cleanup) mutable {
            CefRefPtr<ScreenshotDevToolsObserver> observer =
                new ScreenshotDevToolsObserver(tab_id, std::move(url),
                                               std::move(reply),
                                               std::move(cleanup));
            StartScreenshotDevToolsCapture(host, params, observer);
          },
          host, params, tab_id, std::move(url), std::move(reply),
          std::move(cleanup)),
      75);
}

void BrowserWindow::AppendTabJson(std::string &out, const Tab &tab,
                                  size_t index) const {
  if (!tab.client && !tab.url.empty()) {
    out += "{\"id\":";
    out += tab.id_json;
    out += ",\"index\":";
    AppendJsonNumber(out, index);
    out += ",\"tab\":";
    AppendJsonNumber(out, index + 1);
    out += ",\"active\":";
    AppendJsonBool(out, index == active_index_);
    out += ",\"audible\":";
    AppendJsonBool(out, tab.audible);
    out += ",\"folder_id\":";
    AppendJsonNumber(out, tab.folder_id);
    out += ",\"sidebar_sort_order\":";
    AppendJsonNumber(out, tab.sidebar_sort_order);
    out += ",\"pinned\":";
    AppendJsonBool(out, tab.pinned);
    out += ",\"context\":";
    if (tab.context.empty()) {
      out += "null";
    } else {
      AppendJsonString(out, tab.context);
    }
    out += ",\"url\":";
    out += tab.url_json;
    out += ",\"title\":\"\",\"loading\":false,\"can_go_back\":false,"
           "\"can_go_forward\":false,\"fps_has_sample\":false,\"fps\":null,"
           "\"refresh_rate\":0}";
    return;
  }

  bool fps_has_sample = false;
  double fps = 0.0;
  double refresh_rate = 0.0;
  bool loading = false;
  bool can_go_back = false;
  bool can_go_forward = false;
  std::string url;
  std::string title;

  CefRefPtr<CefBrowser> browser = tab.client ? tab.client->browser() : nullptr;
  if (tab.client) {
    fps_has_sample = tab.client->fps_has_sample();
    fps = tab.client->current_fps();
    refresh_rate = tab.client->compositor_refresh_rate();
  }
  if (browser) {
    loading = browser->IsLoading();
    can_go_back = browser->CanGoBack();
    can_go_forward = browser->CanGoForward();
    if (tab.url.empty() && browser->GetMainFrame()) {
      const std::string frame_url =
          browser->GetMainFrame()->GetURL().ToString();
      if (!frame_url.empty()) {
        url = frame_url;
      }
    }
    if (browser->GetHost()) {
      CefRefPtr<CefNavigationEntry> entry =
          browser->GetHost()->GetVisibleNavigationEntry();
      if (entry) {
        title = entry->GetTitle().ToString();
      }
    }
  }

  out += "{\"id\":";
  if (!tab.id_json.empty()) {
    out += tab.id_json;
  } else {
    AppendJsonNumber(out, tab.id);
  }
  out += ",\"index\":";
  AppendJsonNumber(out, index);
  out += ",\"tab\":";
  AppendJsonNumber(out, index + 1);
  out += ",\"active\":";
  AppendJsonBool(out, index == active_index_);
  out += ",\"audible\":";
  AppendJsonBool(out, tab.audible);
  out += ",\"folder_id\":";
  AppendJsonNumber(out, tab.folder_id);
  out += ",\"sidebar_sort_order\":";
  AppendJsonNumber(out, tab.sidebar_sort_order);
  out += ",\"pinned\":";
  AppendJsonBool(out, tab.pinned);
  out += ",\"context\":";
  if (tab.context.empty()) {
    out += "null";
  } else {
    AppendJsonString(out, tab.context);
  }
  out += ",\"url\":";
  if (!tab.url.empty()) {
    out += tab.url_json;
  } else {
    AppendJsonString(out, url);
  }
  out += ",\"title\":";
  AppendJsonString(out, title);
  out += ",\"loading\":";
  AppendJsonBool(out, loading);
  out += ",\"can_go_back\":";
  AppendJsonBool(out, can_go_back);
  out += ",\"can_go_forward\":";
  AppendJsonBool(out, can_go_forward);
  out += ",\"fps_has_sample\":";
  AppendJsonBool(out, fps_has_sample);
  out += ",\"fps\":";
  if (fps_has_sample) {
    AppendJsonNumber(out, static_cast<int>(std::round(fps)));
  } else {
    out += "null";
  }
  out += ",\"refresh_rate\":";
  if (refresh_rate == 0.0) {
    out.push_back('0');
  } else {
    AppendJsonNumber(out, refresh_rate);
  }
  out += "}";
}

std::string BrowserWindow::TabsJson() const {
  std::string out;
  out.reserve(96 + tabs_.size() * 240);
  out += "{\"ipc_protocol\":\"";
  out += kIpcProtocolName;
  out += "\",\"ipc_version\":";
  AppendJsonNumber(out, kIpcProtocolVersion);
  out += ",\"active_tabid\":";
  AppendJsonNumber(out, ActiveTabId());
  out += ",\"visible_tabid\":";
  AppendJsonNumber(out, visible_tab_index_ < tabs_.size()
                            ? tabs_[visible_tab_index_].id
                            : 0);
  out += ",\"rejected_background_focus_requests\":";
  AppendJsonNumber(out, rejected_background_focus_requests_);
  out += ",\"active_index\":";
  AppendJsonNumber(out, active_index_);
  out += ",\"active_tab\":";
  AppendJsonNumber(out, active_index_ + 1);
  out += ",\"current_folder_id\":";
  AppendJsonNumber(out, current_sidebar_folder_id_);
  out += ",\"sidebar_selected_type\":";
  switch (sidebar_selected_item_.type) {
  case SidebarItemType::kParent:
    AppendJsonString(out, "parent");
    break;
  case SidebarItemType::kFolder:
    AppendJsonString(out, "folder");
    break;
  case SidebarItemType::kTab:
    AppendJsonString(out, "tab");
    break;
  case SidebarItemType::kNone:
    AppendJsonString(out, "none");
    break;
  }
  out += ",\"sidebar_selected_id\":";
  AppendJsonNumber(out, sidebar_selected_item_.id);
  out += ",\"tabs\":[";
  for (size_t i = 0; i < tabs_.size(); ++i) {
    if (i > 0) {
      out.push_back(',');
    }
    AppendTabJson(out, tabs_[i], i);
  }
  out += "]}";
  return out;
}

std::string BrowserWindow::HandleIpcCommand(const std::string &command_line) {
  const std::vector<std::string> argv = SplitArgs(command_line);
  if (argv.empty()) {
    return "ERR empty command\n";
  }

  const std::string command = ToLowerAscii(argv[0]);
  if (command == "version" || command == "protocol") {
    return IpcVersionJson();
  }
  if (command == "status" || command == "json") {
    RefreshAudibleTabs();
    return IpcStatusJson();
  }
  if (command == "tabs") {
    RefreshAudibleTabs();
    return TabsJson();
  }
  if (command == "commands") {
    return IpcCommandsJson();
  }
  if (command == "upload-file-status" ||
      command == "upload-file-cancel") {
    if (argv.size() != 2) {
      return UploadFileErrorJson(
          "invalid_usage",
          command == "upload-file-status"
              ? "usage: upload-file-status <tabid>"
              : "usage: upload-file-cancel <tabid>");
    }
    uint64_t tab_id = 0;
    if (!ParseUint64Arg(argv[1], &tab_id) || tab_id == 0) {
      return UploadFileErrorJson("invalid_tabid",
                                 "tabid must be a positive integer");
    }
    if (!FindTabIndexById(tab_id)) {
      return UploadFileErrorJson("no_such_tabid",
                                 "no tab has the requested id");
    }
    return command == "upload-file-status"
               ? FileChooserUploadStatusJson(tab_id)
               : CancelFileChooserUpload(tab_id);
  }
  if (command == "folders") {
    return FoldersJson();
  }
  if (command == "sidebar") {
    EnsureSidebarSelection();
    return SidebarJson();
  }
  if (command == "fps") {
    if (Tab *tab = ActiveTab();
        tab && tab->client && tab->client->fps_has_sample()) {
      return std::to_string(
          static_cast<int>(std::round(tab->client->current_fps())));
    }
    return "--";
  }
  if (command == "refresh") {
    if (Tab *tab = ActiveTab(); tab && tab->client) {
      return std::to_string(tab->client->compositor_refresh_rate()) + "\n";
    }
    return "0\n";
  }
  if (command == "url") {
    return ActiveTabUrl();
  }
  if (command == "key") {
    if (argv.size() != 2) {
      return "ERR usage: key <[ctrl+][shift+][alt+][cmd+]key>\n";
    }
    CefRefPtr<CefBrowser> browser = ActiveBrowser();
    CefKeyEvent event;
    if (!browser || !browser->GetHost()) {
      return "ERR active tab has no browser\n";
    }
    if (!ParseSyntheticKeySpec(argv[1], &event)) {
      return "ERR invalid key specification\n";
    }
    const bool handled = HandleBrowserKeyEvent(event);
    CefRefPtr<CefBrowser> key_target = browser;
    if (focus_area_ == FocusArea::kDevTools && devtools_browser_view_ &&
        devtools_browser_view_->GetBrowser()) {
      key_target = devtools_browser_view_->GetBrowser();
    }
    if (!handled && key_target && key_target->GetHost()) {
      key_target->GetHost()->SendKeyEvent(event);
    }
    return IpcStatusJson();
  }
  auto find_tab_index_arg = [&](const std::string &text,
                                std::string *error) -> std::optional<size_t> {
    uint64_t tab_id = 0;
    if (!ParseUint64Arg(text, &tab_id) || tab_id == 0) {
      if (error)
        *error = "ERR invalid tabid\n";
      return std::nullopt;
    }
    std::optional<size_t> index = FindTabIndexById(tab_id);
    if (!index && error) {
      *error = "ERR no such tabid\n";
    }
    return index;
  };
  auto tab_index_or_active = [&](size_t arg_index,
                                 std::string *error) -> std::optional<size_t> {
    if (argv.size() <= arg_index) {
      if (tabs_.empty()) {
        if (error)
          *error = "ERR no tabs\n";
        return std::nullopt;
      }
      return active_index_;
    }
    return find_tab_index_arg(argv[arg_index], error);
  };
  auto parse_folder_id = [&](const std::string &text,
                             uint64_t *folder_id) -> bool {
    return ParseUint64Arg(text, folder_id) && SidebarFolderExists(*folder_id);
  };
  if (command == "folder-create") {
    if (argv.size() < 3) {
      return "ERR usage: folder-create <parent-folderid|0> <name>\n";
    }
    uint64_t parent_id = 0;
    if (!parse_folder_id(argv[1], &parent_id)) {
      return "ERR no such parent folder\n";
    }
    if (CreateSidebarFolder(JoinArgs(argv, 2), parent_id) == 0) {
      return "ERR invalid or duplicate folder name\n";
    }
    return FoldersJson();
  }
  if (command == "folder-rename") {
    if (argv.size() < 3) {
      return "ERR usage: folder-rename <folderid> <name>\n";
    }
    uint64_t folder_id = 0;
    if (!parse_folder_id(argv[1], &folder_id) || folder_id == 0) {
      return "ERR no such folder\n";
    }
    if (!RenameSidebarFolder(folder_id, JoinArgs(argv, 2))) {
      return "ERR invalid or duplicate folder name\n";
    }
    return FoldersJson();
  }
  if (command == "folder-delete") {
    if (argv.size() != 3) {
      return "ERR usage: folder-delete <folderid> <recursive|unwrap>\n";
    }
    uint64_t folder_id = 0;
    if (!parse_folder_id(argv[1], &folder_id) || folder_id == 0) {
      return "ERR no such folder\n";
    }
    const std::string mode = ToLowerAscii(argv[2]);
    if (mode != "recursive" && mode != "unwrap") {
      return "ERR usage: folder-delete <folderid> <recursive|unwrap>\n";
    }
    DeleteSidebarFolder(folder_id, mode == "unwrap");
    return FoldersJson();
  }
  if (command == "folder-move") {
    if (argv.size() != 3) {
      return "ERR usage: folder-move <folderid> <parent-folderid|0>\n";
    }
    uint64_t folder_id = 0;
    uint64_t parent_id = 0;
    if (!parse_folder_id(argv[1], &folder_id) || folder_id == 0) {
      return "ERR no such folder\n";
    }
    if (!parse_folder_id(argv[2], &parent_id)) {
      return "ERR no such parent folder\n";
    }
    if (!MoveSidebarItems({{SidebarItemType::kFolder, folder_id}}, parent_id)) {
      return "ERR invalid folder move\n";
    }
    return FoldersJson();
  }
  if (command == "tab-folder") {
    if (argv.size() != 3) {
      return "ERR usage: tab-folder <tabid> <folderid|0>\n";
    }
    std::string error;
    const std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
    if (!index) {
      return error;
    }
    uint64_t folder_id = 0;
    if (!parse_folder_id(argv[2], &folder_id)) {
      return "ERR no such folder\n";
    }
    MoveSidebarItems({{SidebarItemType::kTab, tabs_[*index].id}}, folder_id);
    return TabsJson();
  }
  if (command == "folder-pin") {
    if (argv.size() < 2 || argv.size() > 3) {
      return "ERR usage: folder-pin <folderid> [on|off]\n";
    }
    uint64_t folder_id = 0;
    if (!parse_folder_id(argv[1], &folder_id) || folder_id == 0) {
      return "ERR no such folder\n";
    }
    const SidebarFolder *folder = FindSidebarFolder(folder_id);
    bool pinned = !folder->pinned;
    if (argv.size() == 3) {
      const std::string value = ToLowerAscii(argv[2]);
      if (value == "on" || value == "1" || value == "true") {
        pinned = true;
      } else if (value == "off" || value == "0" || value == "false") {
        pinned = false;
      } else {
        return "ERR usage: folder-pin <folderid> [on|off]\n";
      }
    }
    SetFolderPinned(folder_id, pinned);
    return FoldersJson();
  }
  if (command == "tab-pin") {
    if (argv.size() < 2 || argv.size() > 3) {
      return "ERR usage: tab-pin <tabid> [on|off]\n";
    }
    std::string error;
    const std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
    if (!index) {
      return error;
    }
    bool pinned = !tabs_[*index].pinned;
    if (argv.size() == 3) {
      const std::string value = ToLowerAscii(argv[2]);
      if (value == "on" || value == "1" || value == "true") {
        pinned = true;
      } else if (value == "off" || value == "0" || value == "false") {
        pinned = false;
      } else {
        return "ERR usage: tab-pin <tabid> [on|off]\n";
      }
    }
    SetTabPinned(tabs_[*index].id, pinned);
    return TabsJson();
  }
  if (command == "sidebar-folder") {
    if (argv.size() != 2) {
      return "ERR usage: sidebar-folder <folderid|0>\n";
    }
    uint64_t folder_id = 0;
    if (!parse_folder_id(argv[1], &folder_id)) {
      return "ERR no such folder\n";
    }
    if (IsSidebarSearchMode()) {
      CancelCommand();
    }
    sidebar_search_highlights_visible_ = false;
    current_sidebar_folder_id_ = folder_id;
    sidebar_selected_item_ = {};
    sidebar_visual_anchor_ = {};
    EnsureSidebarSelection();
    SaveState();
    if (RefreshSidebar()) {
      Layout();
    }
    return FoldersJson();
  }
  if (command == "sidebar-visibility") {
    if (argv.size() != 2) {
      return "ERR usage: sidebar-visibility <on|off|toggle>\n";
    }
    const std::string value = ToLowerAscii(argv[1]);
    bool visible = sidebar_visible_;
    if (value == "on" || value == "1" || value == "true") {
      visible = true;
    } else if (value == "off" || value == "0" || value == "false") {
      visible = false;
    } else if (value == "toggle") {
      visible = !visible;
    } else {
      return "ERR usage: sidebar-visibility <on|off|toggle>\n";
    }
    if (!visible && mode_ != Mode::kNormal) {
      CancelCommand();
    }
    if (visible != sidebar_visible_) {
      sidebar_visible_ = visible;
      if (!visible && focus_area_ == FocusArea::kTabSidebar) {
        SetFocusArea(FocusArea::kWebView);
      } else {
        RefreshSidebar();
        UpdateModeIndicator();
        Layout();
      }
    }
    return SidebarJson();
  }
  if (command == "sidebar-focus") {
    if (argv.size() > 2) {
      return "ERR usage: sidebar-focus [sidebar|web]\n";
    }
    FocusArea target = FocusArea::kTabSidebar;
    if (argv.size() == 2) {
      const std::string value = ToLowerAscii(argv[1]);
      if (value == "sidebar" || value == "on") {
        target = FocusArea::kTabSidebar;
      } else if (value == "web" || value == "off") {
        target = FocusArea::kWebView;
      } else {
        return "ERR usage: sidebar-focus [sidebar|web]\n";
      }
    }
    if (mode_ != Mode::kNormal) {
      CancelCommand();
    }
    SetFocusArea(target);
    return SidebarJson();
  }
  if (command == "sidebar-select") {
    if (argv.size() < 2 || argv.size() > 3) {
      return "ERR usage: sidebar-select <tab|folder|parent> [id]\n";
    }
    if (IsSidebarSearchMode()) {
      CancelCommand();
    }
    sidebar_search_highlights_visible_ = false;
    const std::string type = ToLowerAscii(argv[1]);
    if (type == "tab") {
      if (argv.size() != 3) {
        return "ERR usage: sidebar-select tab <tabid>\n";
      }
      std::string error;
      const std::optional<size_t> index = find_tab_index_arg(argv[2], &error);
      if (!index) {
        return error;
      }
      current_sidebar_folder_id_ = tabs_[*index].folder_id;
      sidebar_selected_item_ = {SidebarItemType::kTab, tabs_[*index].id};
    } else if (type == "folder") {
      if (argv.size() != 3) {
        return "ERR usage: sidebar-select folder <folderid>\n";
      }
      uint64_t folder_id = 0;
      if (!parse_folder_id(argv[2], &folder_id) || folder_id == 0) {
        return "ERR no such folder\n";
      }
      const SidebarFolder *folder = FindSidebarFolder(folder_id);
      current_sidebar_folder_id_ = folder->parent_id;
      sidebar_selected_item_ = {SidebarItemType::kFolder, folder_id};
    } else if (type == "parent") {
      if (argv.size() != 2) {
        return "ERR usage: sidebar-select parent\n";
      }
      if (current_sidebar_folder_id_ == 0) {
        return "ERR sidebar is already at root\n";
      }
      sidebar_selected_item_ = {SidebarItemType::kParent, 0};
    } else {
      return "ERR usage: sidebar-select <tab|folder|parent> [id]\n";
    }
    sidebar_visual_anchor_ = {};
    sidebar_pending_keys_.clear();
    SaveState();
    RefreshSidebar();
    Layout();
    return SidebarJson();
  }
  if (command == "sidebar-activate") {
    if (argv.size() != 1) {
      return "ERR usage: sidebar-activate\n";
    }
    if (IsSidebarSearchMode()) {
      CommitSidebarSearch();
    }
    ActivateSidebarItem(sidebar_selected_item_);
    return SidebarJson();
  }
  if (command == "sidebar-search") {
    const std::string usage =
        "ERR usage: sidebar-search <forward|backward> <query> | "
        "sidebar-search next [same|opposite|forward|backward] | "
        "sidebar-search clear\n";
    if (argv.size() == 2 && ToLowerAscii(argv[1]) == "clear") {
      if (IsSidebarSearchMode()) {
        CancelCommand();
      }
      ClearSidebarSearchHighlights();
      return SidebarJson();
    }
    if (argv.size() >= 2 && ToLowerAscii(argv[1]) == "next") {
      if (argv.size() > 3 || sidebar_search_query_.empty()) {
        return sidebar_search_query_.empty() ? "ERR no sidebar search\n"
                                             : usage;
      }
      bool forward = sidebar_search_forward_;
      if (argv.size() == 3) {
        const std::string direction = ToLowerAscii(argv[2]);
        if (direction == "opposite") {
          forward = !sidebar_search_forward_;
        } else if (direction == "forward") {
          forward = true;
        } else if (direction == "backward") {
          forward = false;
        } else if (direction != "same") {
          return usage;
        }
      }
      if (!JumpSidebarSearch(forward)) {
        return "ERR no sidebar search match\n";
      }
      return SidebarJson();
    }
    if (argv.size() < 3) {
      return usage;
    }
    const std::string direction = ToLowerAscii(argv[1]);
    const bool forward = direction == "forward";
    if (!forward && direction != "backward") {
      return usage;
    }
    const std::string query = JoinArgs(argv, 2);
    if (query.empty()) {
      return usage;
    }
    if (mode_ != Mode::kNormal) {
      CancelCommand();
    }
    sidebar_search_query_ = query;
    sidebar_search_forward_ = forward;
    sidebar_search_highlights_visible_ = true;
    sidebar_visual_anchor_ = {};
    if (const std::optional<SidebarItemRef> match =
            FindSidebarSearchMatch(query, sidebar_selected_item_, forward)) {
      sidebar_selected_item_ = *match;
    }
    RefreshSidebar();
    Layout();
    return SidebarJson();
  }
  if (command == "tab-focus") {
    if (argv.size() != 2) {
      return "ERR usage: tab-focus <tabid>\n";
    }
    std::string error;
    std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
    if (!index)
      return error;
    ActivateTab(*index);
    return IpcStatusJson();
  }
  if (command == "tab-delete") {
    if (argv.size() != 2) {
      return "ERR usage: tab-delete <tabid>\n";
    }
    std::string error;
    std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
    if (!index)
      return error;
    CloseTabAtIndex(*index);
    return IpcStatusJson();
  }
  if (command == "tab-order") {
    if (argv.size() != 3) {
      return "ERR usage: tab-order <tabid> <zero-based-index>\n";
    }
    std::string error;
    std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
    if (!index)
      return error;
    long target = 0;
    if (!ParseLongArg(argv[2], &target)) {
      return "ERR invalid target index\n";
    }
    if (target < 0)
      target = 0;
    MoveTabToIndex(*index, static_cast<size_t>(target));
    return TabsJson();
  }
  if (command == "open-tab" || command == "open-background-tab") {
    if (argv.size() < 2) {
      return "ERR usage: open-tab|open-background-tab <url-or-query>\n";
    }
    const std::string text = JoinArgs(argv, 1);
    const std::string url = ResolveUrlOrSearch(text);
    RecordOpenHistory(text);
    const bool activate = command == "open-tab";
    AddTab(url, activate);
    return activate ? IpcStatusJson() : TabsJson();
  }
  if (command == "open-context-tab" ||
      command == "open-background-context-tab") {
    if (argv.size() < 3) {
      return "ERR usage: open-context-tab|open-background-context-tab "
             "<context-name> <url-or-query>\n";
    }
    const std::string text = JoinArgs(argv, 2);
    const std::string url = ResolveUrlOrSearch(text);
    std::string error;
    const bool activate = command == "open-context-tab";
    if (!AddContextTab(argv[1], url, activate, &error)) {
      return error;
    }
    RecordOpenHistory(text);
    return activate ? IpcStatusJson() : TabsJson();
  }
  if (command == "open") {
    if (argv.size() < 3) {
      return "ERR usage: open <tabid> <url-or-query>\n";
    }
    std::string error;
    std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
    if (!index)
      return error;
    const std::string text = JoinArgs(argv, 2);
    const std::string url = ResolveUrlOrSearch(text);
    RecordOpenHistory(text);
    const size_t tab_index = *index;
    Tab &tab = tabs_[tab_index];
    last_tab_close_placeholder_ = false;
    SetTabUrl(tab, url);
    if (tab.client && tab.client->browser() &&
        tab.client->browser()->GetMainFrame()) {
      tab.client->browser()->GetMainFrame()->LoadURL(url);
    }
    SaveState();
    RefreshSidebar();
    Layout();
    return TabsJson();
  }
  if (command == "reload" || command == "reload-ignore-cache" ||
      command == "back" || command == "forward" || command == "stop") {
    if (argv.size() > 2) {
      return "ERR usage: reload|reload-ignore-cache|back|forward|stop "
             "[tabid]\n";
    }
    std::string error;
    std::optional<size_t> index = tab_index_or_active(1, &error);
    if (!index)
      return error;
    Tab &tab = tabs_[*index];
    CefRefPtr<CefBrowser> browser =
        tab.client ? tab.client->browser() : nullptr;
    if (!browser || !browser->GetHost()) {
      return "ERR tab has no browser\n";
    }
    if (command == "reload") {
      browser->Reload();
    } else if (command == "reload-ignore-cache") {
      browser->ReloadIgnoreCache();
    } else if (command == "back") {
      if (browser->CanGoBack())
        browser->GoBack();
    } else if (command == "forward") {
      if (browser->CanGoForward())
        browser->GoForward();
    } else if (command == "stop") {
      browser->StopLoad();
    }
    return TabsJson();
  }
  if (command == "zoom") {
    if (argv.size() != 2 && argv.size() != 3) {
      return "ERR usage: zoom [tabid] <in|out|reset|level>\n";
    }
    size_t arg = 1;
    std::optional<size_t> index = active_index_;
    if (argv.size() == 3) {
      std::string error;
      index = find_tab_index_arg(argv[1], &error);
      if (!index)
        return error;
      arg = 2;
    }
    Tab &tab = tabs_[*index];
    CefRefPtr<CefBrowser> browser =
        tab.client ? tab.client->browser() : nullptr;
    if (!browser || !browser->GetHost()) {
      return "ERR tab has no browser\n";
    }
    const std::string op = ToLowerAscii(argv[arg]);
    if (op == "in" || op == "+") {
      browser->GetHost()->Zoom(CEF_ZOOM_COMMAND_IN);
    } else if (op == "out" || op == "-") {
      browser->GetHost()->Zoom(CEF_ZOOM_COMMAND_OUT);
    } else if (op == "reset" || op == "0") {
      browser->GetHost()->Zoom(CEF_ZOOM_COMMAND_RESET);
    } else {
      double level = 0.0;
      if (!ParseDoubleArg(op, &level)) {
        return "ERR usage: zoom [tabid] <in|out|reset|level>\n";
      }
      browser->GetHost()->SetZoomLevel(level);
    }
    return TabsJson();
  }
  if (command == "scroll-tab") {
    if (argv.size() < 3 || argv.size() > 4) {
      return "ERR usage: scroll-tab <tabid> <dy> [count]\n";
    }
    std::string error;
    std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
    if (!index)
      return error;
    long dy = 0;
    if (!ParseLongArg(argv[2], &dy)) {
      return "ERR invalid dy\n";
    }
    long count_long = 1;
    if (argv.size() >= 4 && !ParseLongArg(argv[3], &count_long)) {
      return "ERR invalid count\n";
    }
    const int count = std::clamp(static_cast<int>(count_long), 1, 100);
    Tab &tab = tabs_[*index];
    CefRefPtr<CefBrowser> browser =
        tab.client ? tab.client->browser() : nullptr;
    if (!browser || !browser->GetMainFrame()) {
      return "ERR tab has no browser\n";
    }
    std::ostringstream script;
    script << "window.scrollBy(0," << (dy * count) << ");";
    browser->GetMainFrame()->ExecuteJavaScript(
        script.str(), browser->GetMainFrame()->GetURL(), 0);
    return TabsJson();
  }
  if (command == "showfps") {
    if (argv.size() == 1) {
      SetShowFpsIndicator(!show_fps_indicator_);
      return IpcStatusJson();
    }
    const std::string arg = ToLowerAscii(argv[1]);
    if (arg == "on" || arg == "1" || arg == "true") {
      SetShowFpsIndicator(true);
      return IpcStatusJson();
    }
    if (arg == "off" || arg == "0" || arg == "false") {
      SetShowFpsIndicator(false);
      return IpcStatusJson();
    }
    return "ERR usage: showfps [on|off]\n";
  }
  if (command == "showstatusline") {
    if (argv.size() == 1) {
      SetShowStatusLine(!show_statusline_);
      return IpcStatusJson();
    }
    const std::string arg = ToLowerAscii(argv[1]);
    if (arg == "on" || arg == "1" || arg == "true") {
      SetShowStatusLine(true);
      return IpcStatusJson();
    }
    if (arg == "off" || arg == "0" || arg == "false") {
      SetShowStatusLine(false);
      return IpcStatusJson();
    }
    return "ERR usage: showstatusline [on|off]\n";
  }
  if (command == "shader") {
    if (argv.size() == 1) {
      SetShaderEnabled(!shader_enabled_);
      return IpcStatusJson();
    }
    const std::string arg = ToLowerAscii(argv[1]);
    if (arg == "on" || arg == "1" || arg == "true") {
      SetShaderEnabled(true);
      return IpcStatusJson();
    }
    if (arg == "off" || arg == "0" || arg == "false") {
      SetShaderEnabled(false);
      return IpcStatusJson();
    }
    return "ERR usage: shader [on|off]\n";
  }
  if (command == "scroll") {
    if (argv.size() < 2) {
      return "ERR usage: scroll <dy> [count]\n";
    }
    char *end = nullptr;
    const long dy = std::strtol(argv[1].c_str(), &end, 10);
    if (end == argv[1].c_str()) {
      return "ERR invalid dy\n";
    }
    int count = 1;
    if (argv.size() >= 3) {
      char *count_end = nullptr;
      count = static_cast<int>(std::strtol(argv[2].c_str(), &count_end, 10));
      if (count_end == argv[2].c_str()) {
        return "ERR invalid count\n";
      }
    }
    count = std::clamp(count, 1, 100);
    for (int i = 0; i < count; ++i) {
      ScrollActivePageBy(static_cast<int>(dy));
    }
    return IpcStatusJson();
  }
  if (command == "tab") {
    if (argv.size() != 2) {
      return "ERR usage: tab <1-based-index>\n";
    }
    char *end = nullptr;
    const long index = std::strtol(argv[1].c_str(), &end, 10);
    if (end == argv[1].c_str() || index <= 0) {
      return "ERR invalid tab index\n";
    }
    ActivateTab(static_cast<size_t>(index - 1));
    return IpcStatusJson();
  }
  if (command == "tab-close") {
    if (argv.size() == 1) {
      CloseActiveTab();
      return IpcStatusJson();
    }
    if (argv.size() == 2) {
      std::string error;
      std::optional<size_t> index = find_tab_index_arg(argv[1], &error);
      if (!index)
        return error;
      CloseTabAtIndex(*index);
      return IpcStatusJson();
    }
    return "ERR usage: tab-close [tabid]\n";
  }
  if (command == "help") {
    return "commands:\n"
           "  version|protocol\n"
           "  status|json\n"
           "  tabs\n"
           "  commands\n"
           "  folders\n"
           "  folder-create <parent-folderid|0> <name>\n"
           "  folder-rename <folderid> <name>\n"
           "  folder-delete <folderid> <recursive|unwrap>\n"
           "  folder-move <folderid> <parent-folderid|0>\n"
           "  folder-pin <folderid> [on|off]\n"
           "  tab-folder <tabid> <folderid|0>\n"
           "  tab-pin <tabid> [on|off]\n"
           "  sidebar\n"
           "  sidebar-folder <folderid|0>\n"
           "  sidebar-visibility <on|off|toggle>\n"
           "  sidebar-focus [sidebar|web]\n"
           "  sidebar-select <tab|folder|parent> [id]\n"
           "  sidebar-activate\n"
           "  sidebar-search <forward|backward> <query>\n"
           "  sidebar-search next [same|opposite|forward|backward]\n"
           "  sidebar-search clear\n"
           "  tab-focus <tabid>\n"
           "  tab-delete <tabid>\n"
           "  tab-order <tabid> <zero-based-index>\n"
           "  open-tab <url-or-query>\n"
           "  open-background-tab <url-or-query>\n"
           "  open-context-tab <context-name> <url-or-query>\n"
           "  open-background-context-tab <context-name> <url-or-query>\n"
           "  open <tabid> <url-or-query>\n"
           "  reload [tabid]\n"
           "  reload-ignore-cache [tabid]\n"
           "  back [tabid]\n"
           "  forward [tabid]\n"
           "  stop [tabid]\n"
           "  zoom [tabid] <in|out|reset|level>\n"
           "  scroll <dy> [count]\n"
           "  scroll-tab <tabid> <dy> [count]\n"
           "  frame-tree <tabid>\n"
           "  inspect-controls <tabid> <base64-v1-json-query>\n"
           "  activate-control <tabid> <exact-node-handle>\n"
           "  key <[ctrl+][shift+][alt+][cmd+]key>\n"
           "  html <tabid>\n"
           "  text <tabid>\n"
           "  frame-html <tabid> <frameid>\n"
           "  frame-text <tabid> <frameid>\n"
           "  screenshot <tabid>\n"
           "  js <tabid> <javascript>\n"
           "  frame-js <tabid> <frameid> <javascript>\n"
           "  js-base64 <tabid> <base64-utf8-javascript>\n"
           "  frame-js-base64 <tabid> <frameid> <base64-utf8-javascript>\n"
           "  js-file <tabid> <path>\n"
           "  upload-file <tabid> <base64-v1-json-payload>\n"
           "  upload-file-status <tabid>\n"
           "  upload-file-cancel <tabid>\n"
           "  cookies <tabid> [url]\n"
           "  cookies-url <url>\n"
           "  cookie-delete <tabid> <name>\n"
           "  cookie-set <tabid> <name> <value> [domain] [path]\n"
           "  network <tabid> list\n"
           "  network <tabid> detail <requestid>\n"
           "  network <tabid> body <requestid>\n"
           "  network <tabid> replay <requestid>\n"
           "  network <tabid> clear\n"
           "  fps\n"
           "  refresh\n"
           "  url\n"
           "  showfps [on|off]\n"
           "  showstatusline [on|off]\n"
           "  shader [on|off]\n"
           "  tab <1-based-index>\n"
           "  tab-close [tabid]\n";
  }
  return "ERR unknown command\n";
}

void BrowserWindow::HandleIpcCommandAsync(const std::string &command_line,
                                          IpcReplyCallback reply) {
  const std::vector<std::string> argv = SplitArgs(command_line);
  if (argv.empty()) {
    reply("ERR empty command\n");
    return;
  }

  const std::string command = ToLowerAscii(argv[0]);
  if (command == "upload-file") {
    if (argv.size() != 3) {
      reply(UploadFileErrorJson(
          "invalid_usage",
          "usage: upload-file <tabid> <base64-v1-json-payload>"));
      return;
    }
    uint64_t tab_id = 0;
    if (!ParseUint64Arg(argv[1], &tab_id) || tab_id == 0) {
      reply(UploadFileErrorJson("invalid_tabid", "tabid must be a positive integer"));
      return;
    }
    if (!FindTabIndexById(tab_id)) {
      reply(UploadFileErrorJson("no_such_tabid", "no tab has the requested id"));
      return;
    }

    UploadFileRequest request;
    request.tab_id = tab_id;
    std::string payload_error;
    if (!DecodeUploadFilePayload(argv[2], &request, &payload_error)) {
      reply(std::move(payload_error));
      return;
    }

    CefRefPtr<BrowserWindow> self = this;
    if (!CefPostTask(
            TID_FILE_USER_BLOCKING,
            base::BindOnce(
                [](CefRefPtr<BrowserWindow> self, UploadFileRequest request,
                   IpcReplyCallback reply) mutable {
                  UploadFileValidation validation =
                      ValidateUploadFilePaths(request.paths);
                  if (!validation.ok) {
                    reply(std::move(validation.error));
                    return;
                  }
                  request.paths = std::move(validation.canonical_paths);
                  if (!CefPostTask(
                          TID_UI,
                          base::BindOnce(
                              [](CefRefPtr<BrowserWindow> self,
                                 UploadFileRequest request,
                                 IpcReplyCallback reply) mutable {
                                std::string ignored_error;
                                CefRefPtr<CefBrowser> browser =
                                    self->BrowserForTabId(request.tab_id,
                                                          &ignored_error);
                                if (!browser || !browser->GetHost()) {
                                  reply(UploadFileErrorJson(
                                      "tab_unavailable",
                                      "tab closed before file assignment"));
                                  return;
                                }
                                if (request.target_kind == "chooser") {
                                  reply(self->ArmFileChooserUpload(
                                      request.tab_id,
                                      std::move(request.paths)));
                                  return;
                                }
                                if (request.target_kind == "activate") {
                                  self->StartFileChooserActivationUpload(
                                      request.tab_id,
                                      std::move(request.selector),
                                      std::move(request.paths),
                                      std::move(reply));
                                  return;
                                }
                                if (request.target_kind == "handle") {
                                  self->StartFileChooserHandleUpload(
                                      request.tab_id,
                                      std::move(request.handle),
                                      std::move(request.paths),
                                      std::move(reply));
                                  return;
                                }
                                StartUploadFileAssignment(
                                    browser->GetHost(), std::move(request),
                                    std::move(reply));
                              },
                              self, std::move(request), reply))) {
                    reply(UploadFileErrorJson(
                        "internal_error",
                        "failed to return file validation to the UI thread"));
                  }
                },
                self, std::move(request), reply))) {
      reply(UploadFileErrorJson(
          "internal_error", "failed to start browser-side file validation"));
    }
    return;
  }

  auto parse_tab_id = [&](size_t arg_index, uint64_t* tab_id) -> bool {
    if (argv.size() <= arg_index ||
        !ParseUint64Arg(argv[arg_index], tab_id) || *tab_id == 0) {
      reply("ERR invalid tabid\n");
      return false;
    }
    if (!FindTabIndexById(*tab_id)) {
      reply("ERR no such tabid\n");
      return false;
    }
    return true;
  };

  if (command == "frame-tree") {
    if (argv.size() != 2) {
      reply(UploadFileErrorJson("invalid_usage",
                                "usage: frame-tree <tabid>"));
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    reply(FrameTreeJson(tab_id));
    return;
  }

  if (command == "inspect-controls") {
    if (argv.size() != 3) {
      reply(UploadFileErrorJson(
          "invalid_usage",
          "usage: inspect-controls <tabid> <base64-v1-json-query>"));
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    HandleInspectControlsIpcCommand(tab_id, argv[2], std::move(reply));
    return;
  }

  if (command == "activate-control") {
    if (argv.size() != 3) {
      reply(UploadFileErrorJson(
          "invalid_usage",
          "usage: activate-control <tabid> <exact-node-handle>"));
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    const std::string& handle = argv[2];
    if (!handle.starts_with("eh1_") || handle.size() > 128) {
      reply(UploadFileErrorJson("invalid_handle",
                                "inspected element handle is malformed"));
      return;
    }
    std::string browser_error;
    CefRefPtr<CefBrowser> browser = BrowserForTabId(tab_id, &browser_error);
    if (!browser || !browser->GetHost()) {
      reply(UploadFileErrorJson("tab_unavailable",
                                "tab has no live browser backend"));
      return;
    }

    auto* context = new ActivateControlContext{tab_id, std::move(reply)};
    uint64_t ignored_nonce_high = 0;
    uint64_t ignored_nonce_low = 0;
    const bool started = vimbrowser_activate_element_handle(
        browser->GetIdentifier(), handle.data(), handle.size(), true,
        &ignored_nonce_high, &ignored_nonce_low,
        +[](void* user_data, int result, int match_count) {
          std::unique_ptr<ActivateControlContext> context(
              static_cast<ActivateControlContext*>(user_data));
          if (!context || !context->reply) {
            return;
          }
          const auto activation_result =
              static_cast<VimbrowserElementActivationResult>(result);
          if (activation_result !=
              VimbrowserElementActivationResult::kDispatched) {
            const VimbrowserElementActivationError error =
                ElementActivationErrorForResult(activation_result);
            context->reply(UploadFileErrorJson(
                error.code, error.message, -1,
                activation_result ==
                        VimbrowserElementActivationResult::kAmbiguousTarget
                    ? match_count
                    : -1));
            return;
          }
          std::ostringstream out;
          out << "{\"ok\":true,\"tabid\":" << context->tab_id
              << ",\"target\":{\"kind\":\"handle\"},"
                 "\"activation\":{\"dispatched\":true,"
                 "\"user_activation\":true}}";
          context->reply(out.str());
        },
        context);
    if (!started) {
      IpcReplyCallback startup_reply = std::move(context->reply);
      delete context;
      startup_reply(UploadFileErrorJson(
          "activation_backend_unavailable",
          "custom Chromium element activation backend is unavailable"));
    }
    return;
  }

  if (command == "html" || command == "text" ||
      command == "frame-html" || command == "frame-text") {
    const bool frame_specific = command.starts_with("frame-");
    if (argv.size() != (frame_specific ? 3U : 2U)) {
      reply("ERR usage: html|text <tabid> OR frame-html|frame-text <tabid> <frameid>\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    HandleHtmlIpcCommand(tab_id, frame_specific ? argv[2] : std::string(),
                         command == "text" || command == "frame-text",
                         std::move(reply));
    return;
  }

  if (command == "screenshot") {
    if (argv.size() != 2) {
      reply("ERR usage: screenshot <tabid>\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    HandleScreenshotIpcCommand(tab_id, std::move(reply));
    return;
  }

  if (command == "js" || command == "frame-js") {
    const bool frame_specific = command == "frame-js";
    if (argv.size() < (frame_specific ? 4U : 3U)) {
      reply("ERR usage: js <tabid> <javascript> OR frame-js <tabid> <frameid> <javascript>\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    HandleJsIpcCommand(tab_id, frame_specific ? argv[2] : std::string(),
                       JoinArgs(argv, frame_specific ? 3 : 2),
                       std::move(reply));
    return;
  }

  if (command == "js-base64" || command == "frame-js-base64") {
    const bool frame_specific = command == "frame-js-base64";
    if (argv.size() != (frame_specific ? 4U : 3U)) {
      reply("ERR usage: js-base64 <tabid> <base64-utf8-javascript> OR frame-js-base64 <tabid> <frameid> <base64-utf8-javascript>\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    std::string code;
    std::string error;
    if (!DecodeBase64JsPayload(argv[frame_specific ? 3 : 2], &code,
                               &error)) {
      reply("ERR " + error + "\n");
      return;
    }
    HandleJsIpcCommand(tab_id, frame_specific ? argv[2] : std::string(),
                       std::move(code), std::move(reply));
    return;
  }

  if (command == "js-file") {
    if (argv.size() != 3) {
      reply("ERR usage: js-file <tabid> <path>\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    CefRefPtr<BrowserWindow> self = this;
    if (!CefPostTask(TID_FILE_USER_BLOCKING,
                     base::BindOnce(&BrowserWindow::ReadJsFileForIpc, self,
                                    tab_id, argv[2], reply))) {
      reply("ERR failed to post js-file read task\n");
    }
    return;
  }

  if (command == "cookies") {
    if (argv.size() < 2) {
      reply("ERR usage: cookies <tabid> [url]\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    HandleCookiesIpcCommand(
        tab_id, argv.size() >= 3 ? JoinArgs(argv, 2) : std::string(),
        std::move(reply));
    return;
  }

  if (command == "cookies-url") {
    if (argv.size() < 2) {
      reply("ERR usage: cookies-url <url>\n");
      return;
    }
    HandleCookiesForUrlIpcCommand(JoinArgs(argv, 1), std::move(reply));
    return;
  }

  if (command == "cookie-delete") {
    if (argv.size() != 3) {
      reply("ERR usage: cookie-delete <tabid> <name>\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    HandleCookieDeleteIpcCommand(tab_id, argv[2], std::move(reply));
    return;
  }

  if (command == "cookie-set") {
    if (argv.size() < 4 || argv.size() > 6) {
      reply("ERR usage: cookie-set <tabid> <name> <value> [domain] [path]\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    HandleCookieSetIpcCommand(
        tab_id, argv[2], argv[3], argv.size() >= 5 ? argv[4] : std::string(),
        argv.size() >= 6 ? argv[5] : std::string(), std::move(reply));
    return;
  }

  if (command == "network") {
    if (argv.size() < 3) {
      reply("ERR usage: network <tabid> list|detail|body|replay [requestid]\n");
      return;
    }
    uint64_t tab_id = 0;
    if (!parse_tab_id(1, &tab_id)) {
      return;
    }
    std::optional<size_t> index = FindTabIndexById(tab_id);
    if (!index || !tabs_[*index].client) {
      reply("ERR tab has no client\n");
      return;
    }
    const std::string subcommand = ToLowerAscii(argv[2]);
    if (subcommand == "list" || subcommand == "clear") {
      if (argv.size() != 3) {
        reply("ERR usage: network <tabid> list|clear\n");
        return;
      }
      if (subcommand == "clear") {
        tabs_[*index].client->ClearNetworkLog();
        reply("{\"cleared\":true}");
        return;
      }
      reply(tabs_[*index].client->NetworkListJson());
      return;
    }
    if (subcommand == "detail" || subcommand == "body" ||
        subcommand == "replay") {
      if (argv.size() != 4) {
        reply("ERR usage: network <tabid> detail|body|replay <requestid>\n");
        return;
      }
      uint64_t request_id = 0;
      if (!ParseUint64Arg(argv[3], &request_id) || request_id == 0) {
        reply("ERR invalid requestid\n");
        return;
      }
      if (subcommand == "detail") {
        reply(tabs_[*index].client->NetworkDetailJson(request_id));
        return;
      }
      if (subcommand == "body") {
        std::string body;
        std::string error;
        if (!tabs_[*index].client->NetworkBody(request_id, &body, &error)) {
          reply(error);
          return;
        }
        reply(std::move(body));
        return;
      }
      HandleNetworkReplayIpcCommand(tab_id, request_id, std::move(reply));
      return;
    }
    reply("ERR usage: network <tabid> list|detail|body|replay [requestid]\n");
    return;
  }

  reply(HandleIpcCommand(command_line));
}

std::string BrowserWindow::IpcStatusJson() const {
  bool fps_has_sample = false;
  double fps = 0.0;
  double refresh_rate = 0.0;
  std::string url;
  std::string title;
  std::string context;
  bool audible = false;
  if (!tabs_.empty() && active_index_ < tabs_.size()) {
    const Tab &tab = tabs_[active_index_];
    url = tab.url;
    context = tab.context;
    audible = tab.audible;
    if (tab.client) {
      fps_has_sample = tab.client->fps_has_sample();
      fps = tab.client->current_fps();
      refresh_rate = tab.client->compositor_refresh_rate();
    }
    if (CefRefPtr<CefBrowser> browser =
            tab.client ? tab.client->browser() : nullptr;
        browser && browser->GetHost()) {
      CefRefPtr<CefNavigationEntry> entry =
          browser->GetHost()->GetVisibleNavigationEntry();
      if (entry) {
        title = entry->GetTitle().ToString();
      }
    }
  }

  std::string out;
  out.reserve(256 + url.size() + title.size());
  out += "{\"ipc_protocol\":\"";
  out += kIpcProtocolName;
  out += "\",\"ipc_version\":";
  AppendJsonNumber(out, kIpcProtocolVersion);
  out += ",\"active_tabid\":";
  AppendJsonNumber(out, ActiveTabId());
  out += ",\"visible_tabid\":";
  AppendJsonNumber(out, visible_tab_index_ < tabs_.size()
                            ? tabs_[visible_tab_index_].id
                            : 0);
  out += ",\"rejected_background_focus_requests\":";
  AppendJsonNumber(out, rejected_background_focus_requests_);
  out += ",\"active_index\":";
  AppendJsonNumber(out, active_index_);
  out += ",\"active_tab\":";
  AppendJsonNumber(out, active_index_ + 1);
  out += ",\"tabs\":";
  AppendJsonNumber(out, tabs_.size());
  out += ",\"current_folder_id\":";
  AppendJsonNumber(out, current_sidebar_folder_id_);
  out += ",\"sidebar_selected_type\":";
  switch (sidebar_selected_item_.type) {
  case SidebarItemType::kParent:
    AppendJsonString(out, "parent");
    break;
  case SidebarItemType::kFolder:
    AppendJsonString(out, "folder");
    break;
  case SidebarItemType::kTab:
    AppendJsonString(out, "tab");
    break;
  case SidebarItemType::kNone:
    AppendJsonString(out, "none");
    break;
  }
  out += ",\"sidebar_selected_id\":";
  AppendJsonNumber(out, sidebar_selected_item_.id);
  out += ",\"url\":";
  AppendJsonString(out, url);
  out += ",\"title\":";
  AppendJsonString(out, title);
  out += ",\"context\":";
  if (context.empty()) {
    out += "null";
  } else {
    AppendJsonString(out, context);
  }
  out += ",\"audible\":";
  AppendJsonBool(out, audible);
  out += ",\"showfps\":";
  AppendJsonBool(out, show_fps_indicator_);
  out += ",\"showstatusline\":";
  AppendJsonBool(out, show_statusline_);
  out += ",\"shader\":";
  AppendJsonBool(out, shader_enabled_);
  out += ",\"mode\":";
  AppendJsonString(out, ModeIndicatorText());
  out += ",\"fps_has_sample\":";
  AppendJsonBool(out, fps_has_sample);
  out += ",\"fps\":";
  if (fps_has_sample) {
    AppendJsonNumber(out, static_cast<int>(std::round(fps)));
  } else {
    out += "null";
  }
  out += ",\"refresh_rate\":";
  AppendJsonNumber(out, refresh_rate);
  out += "}";
  return out;
}

} // namespace vimbrowser
