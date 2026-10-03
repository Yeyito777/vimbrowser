#pragma once

#include "include/cef_client.h"
#include "include/cef_dialog_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_focus_handler.h"
#include "include/cef_jsdialog_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_request.h"
#include "include/cef_request_handler.h"
#include "include/cef_resource_request_handler.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vimbrowser {

class BrowserWindow;

struct NetworkRequestMutation {
  std::optional<std::string> url;
  std::optional<std::string> method;
  std::optional<std::string> body;
  std::vector<std::pair<std::string, std::string>> header_overrides;
  std::vector<std::string> remove_headers;
};

class BrowserClient final : public CefClient,
                            public CefDialogHandler,
                            public CefDisplayHandler,
                            public CefFocusHandler,
                            public CefJSDialogHandler,
                            public CefLifeSpanHandler,
                            public CefLoadHandler,
                            public CefKeyboardHandler,
                            public CefContextMenuHandler,
                            public CefRequestHandler,
                            public CefResourceRequestHandler,
                            public CefPermissionHandler {
public:
  explicit BrowserClient(BrowserWindow *owner = nullptr);

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefDialogHandler> GetDialogHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefFocusHandler> GetFocusHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
    return this;
  }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override {
    return this;
  }

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforePopupAborted(CefRefPtr<CefBrowser> browser,
                            int popup_id) override;
  bool OnFileDialog(
      CefRefPtr<CefBrowser> browser,
      FileDialogMode mode,
      const CefString& title,
      const CefString& default_file_path,
      const std::vector<CefString>& accept_filters,
      const std::vector<CefString>& accept_extensions,
      const std::vector<CefString>& accept_descriptions,
      CefRefPtr<CefFileDialogCallback> callback) override;
  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  bool OnBeforeUnloadDialog(
      CefRefPtr<CefBrowser> browser,
      const CefString& message_text,
      bool is_reload,
      CefRefPtr<CefJSDialogCallback> callback) override;
  bool OnSetFocus(CefRefPtr<CefBrowser> browser, FocusSource source) override;
#if CEF_API_ADDED(13700)
  bool GetRootWindowScreenRect(CefRefPtr<CefBrowser> browser,
                               CefRect &rect) override;
#endif
  void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   ErrorCode error_code, const CefString &error_text,
                   const CefString &failed_url) override;
  void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   TransitionType transition_type) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                            bool isLoading,
                            bool canGoBack,
                            bool canGoForward) override;
  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int httpStatusCode) override;
  bool OnBeforePopup(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int popup_id,
      const CefString &target_url, const CefString &target_frame_name,
      CefLifeSpanHandler::WindowOpenDisposition target_disposition,
      bool user_gesture, const CefPopupFeatures &popupFeatures,
      CefWindowInfo &windowInfo, CefRefPtr<CefClient> &client,
      CefBrowserSettings &settings, CefRefPtr<CefDictionaryValue> &extra_info,
      bool *no_javascript_access) override;
  bool
  OnOpenURLFromTab(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   const CefString &target_url,
                   CefRequestHandler::WindowOpenDisposition target_disposition,
                   bool user_gesture) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level,
                        const CefString &message, const CefString &source,
                        int line) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request, bool user_gesture,
                      bool is_redirect) override;

  bool OnRequestMediaAccessPermission(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      const CefString &requesting_origin, uint32_t requested_permissions,
      CefRefPtr<CefMediaAccessCallback> callback) override;

  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser, const CefKeyEvent &event,
                     CefEventHandle os_event,
                     bool *is_keyboard_shortcut) override;

  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;
  bool RunContextMenu(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefContextMenuParams> params,
                      CefRefPtr<CefMenuModel> model,
                      CefRefPtr<CefRunContextMenuCallback> callback) override;
  bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefContextMenuParams> params,
                            int command_id,
                            cef_event_flags_t event_flags) override;
  void OnContextMenuDismissed(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame) override;

  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request, bool is_navigation, bool is_download,
      const CefString &request_initiator,
      bool &disable_default_handling) override;
  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request,
                                   CefRefPtr<CefCallback> callback) override;
  bool OnResourceResponse(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefRequest> request,
                          CefRefPtr<CefResponse> response) override;
  CefRefPtr<CefResponseFilter> GetResourceResponseFilter(
      CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request, CefRefPtr<CefResponse> response) override;
  void OnResourceLoadComplete(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              CefRefPtr<CefRequest> request,
                              CefRefPtr<CefResponse> response,
                              URLRequestStatus status,
                              int64_t received_content_length) override;

  CefRefPtr<CefBrowser> browser() const { return browser_; }
  void DetachOwner();
  void ShowDevTools();
  double current_fps() const;
  bool fps_has_sample() const;
  double compositor_refresh_rate() const;
  bool is_currently_audible() const;
  void SendBrowserCommandKeyEvent(const CefKeyEvent &event);
  std::string NetworkListJson() const;
  std::string NetworkDetailJson(uint64_t request_id) const;
  bool NetworkBody(uint64_t request_id, std::string *body,
                   std::string *error) const;
  std::string SetNetworkCapture(bool enabled, std::string url_prefix);
  std::string NetworkCaptureJson() const;
  std::optional<std::string> NetworkMatchJson(
      const std::string& url_prefix, uint64_t after_request_id) const;
  void ClearNetworkLog();
  CefRefPtr<CefRequest> BuildReplayRequest(uint64_t request_id,
                                           std::string *error) const;
  CefRefPtr<CefRequest> BuildDerivedRequest(
      uint64_t request_id, const NetworkRequestMutation& mutation,
      std::string* error) const;
  struct NetworkRequestRecord;

private:
  std::shared_ptr<NetworkRequestRecord>
  FindNetworkRecord(uint64_t request_id) const;
  std::shared_ptr<NetworkRequestRecord>
  FindNetworkRecordByCefId(uint64_t cef_id) const;

  BrowserWindow *owner_ = nullptr;
  CefRefPtr<CefBrowser> browser_;
  std::string pending_chatgpt_autosubmit_prompt_;
  std::string pending_chatgpt_autosubmit_key_;
  uint64_t chatgpt_autosubmit_sequence_ = 0;
  mutable std::mutex network_mutex_;
  uint64_t next_network_request_id_ = 1;
  std::vector<std::shared_ptr<NetworkRequestRecord>> network_log_;
  std::unordered_map<uint64_t, std::shared_ptr<NetworkRequestRecord>>
      active_network_by_cef_id_;
  bool dynamic_network_capture_enabled_ = false;
  std::string dynamic_network_capture_url_prefix_;
  IMPLEMENT_REFCOUNTING(BrowserClient);
  DISALLOW_COPY_AND_ASSIGN(BrowserClient);
};

} // namespace vimbrowser
