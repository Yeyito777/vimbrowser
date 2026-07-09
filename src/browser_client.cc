#include "browser_client.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include "browser_font_settings.h"
#include "browser_window.h"
#include "config.h"
#include "include/cef_app.h"
#include "include/cef_callback.h"
#include "include/cef_response.h"
#include "include/cef_response_filter.h"
#if defined(__APPLE__)
#include "mac/browser_features_mac.h"
#endif

extern "C" bool vimbrowser_browser_has_fps_sample(int browser_id);
extern "C" double vimbrowser_get_browser_fps(int browser_id);
extern "C" double vimbrowser_get_browser_refresh_rate(int browser_id);
extern "C" bool vimbrowser_browser_is_currently_audible(int browser_id);
extern "C" void vimbrowser_send_browser_command_key_event(
    int browser_id,
    const CefKeyEvent* event);

extern "C" bool vimbrowser_get_current_file_dialog_activation_nonce(
    int browser_id,
    uint64_t* activation_nonce_high,
    uint64_t* activation_nonce_low);

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

std::string LowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool ParseBooleanSwitchValue(std::string_view value, bool fallback) {
  if (value == "1" || value == "true" || value == "yes" || value == "on") {
    return true;
  }
  if (value == "0" || value == "false" || value == "no" || value == "off") {
    return false;
  }
  return fallback;
}

bool NativeContentBlockingEnabled() {
  static const bool enabled = [] {
    bool result = true;
    if (const char* env = std::getenv("VIMBROWSER_CONTENT_BLOCKING");
        env && *env) {
      result = ParseBooleanSwitchValue(LowerAscii(env), result);
    }
    CefRefPtr<CefCommandLine> command_line = CefCommandLine::GetGlobalCommandLine();
    if (command_line &&
        command_line->HasSwitch("disable-vimbrowser-content-blocking")) {
      result = false;
    }
    return result;
  }();
  return enabled;
}

bool NativeNetworkCaptureEnabled() {
  static const bool enabled = [] {
    bool result = false;
    if (const char* env = std::getenv("VIMBROWSER_NETWORK_CAPTURE");
        env && *env) {
      result = ParseBooleanSwitchValue(LowerAscii(env), result);
    }
    CefRefPtr<CefCommandLine> command_line = CefCommandLine::GetGlobalCommandLine();
    if (command_line &&
        command_line->HasSwitch("enable-vimbrowser-network-capture")) {
      result = true;
    }
    if (command_line &&
        command_line->HasSwitch("disable-vimbrowser-network-capture")) {
      result = false;
    }
    return result;
  }();
  return enabled;
}

bool HostIsOrSubdomain(std::string_view host, std::string_view domain) {
  if (host == domain) {
    return true;
  }
  return host.size() > domain.size() &&
         host.compare(host.size() - domain.size(), domain.size(), domain) == 0 &&
         host[host.size() - domain.size() - 1] == '.';
}

std::string HostFromUrl(std::string_view url) {
  size_t start = 0;
  if (const size_t scheme = url.find("://"); scheme != std::string_view::npos) {
    start = scheme + 3;
  }
  const size_t authority_end = url.find_first_of("/?#", start);
  std::string_view authority =
      authority_end == std::string_view::npos
          ? url.substr(start)
          : url.substr(start, authority_end - start);
  if (const size_t at = authority.rfind('@'); at != std::string_view::npos) {
    authority.remove_prefix(at + 1);
  }
  if (!authority.empty() && authority.front() == '[') {
    const size_t close = authority.find(']');
    if (close != std::string_view::npos) {
      authority = authority.substr(1, close - 1);
    }
  } else if (const size_t colon = authority.find(':');
             colon != std::string_view::npos) {
    authority = authority.substr(0, colon);
  }
  while (!authority.empty() && authority.back() == '.') {
    authority.remove_suffix(1);
  }
  return LowerAscii(std::string(authority));
}

bool IsChatgptUrl(std::string_view url) {
  const std::string host = HostFromUrl(url);
  return HostIsOrSubdomain(host, "chatgpt.com") ||
         HostIsOrSubdomain(host, "chat.openai.com");
}

bool IsChatgptAuthUrl(std::string_view url) {
  const std::string host = HostFromUrl(url);
  return HostIsOrSubdomain(host, "auth.openai.com");
}

int HexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

std::string UrlDecodeQueryComponent(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    const char c = value[i];
    if (c == '+') {
      out.push_back(' ');
      continue;
    }
    if (c == '%' && i + 2 < value.size()) {
      const int hi = HexValue(value[i + 1]);
      const int lo = HexValue(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(c);
  }
  return out;
}

bool UrlQueryParam(std::string_view url,
                   std::string_view name,
                   std::string* value_out) {
  const size_t question = url.find('?');
  if (question == std::string_view::npos) {
    return false;
  }
  const size_t fragment = url.find('#', question + 1);
  const std::string_view query =
      fragment == std::string_view::npos
          ? url.substr(question + 1)
          : url.substr(question + 1, fragment - question - 1);
  size_t pos = 0;
  while (pos <= query.size()) {
    const size_t amp = query.find('&', pos);
    const std::string_view part =
        amp == std::string_view::npos ? query.substr(pos)
                                      : query.substr(pos, amp - pos);
    const size_t equals = part.find('=');
    const std::string decoded_name = UrlDecodeQueryComponent(
        equals == std::string_view::npos ? part : part.substr(0, equals));
    if (decoded_name == name) {
      if (value_out) {
        *value_out = UrlDecodeQueryComponent(
            equals == std::string_view::npos ? std::string_view()
                                             : part.substr(equals + 1));
      }
      return true;
    }
    if (amp == std::string_view::npos) {
      break;
    }
    pos = amp + 1;
  }
  return false;
}

bool HasNonWhitespace(std::string_view value) {
  return std::any_of(value.begin(), value.end(), [](unsigned char c) {
    return !std::isspace(c);
  });
}

bool ExtractChatgptAutosubmitPrompt(std::string_view url, std::string* prompt) {
  if (!IsChatgptUrl(url)) {
    return false;
  }

  std::string token;
  if (!UrlQueryParam(url, "vimbrowser_autosend", &token) ||
      token != ChatgptAutosendToken()) {
    return false;
  }

  std::string value;
  if ((!UrlQueryParam(url, "vimbrowser_prompt", &value) &&
       !UrlQueryParam(url, "q", &value) &&
       !UrlQueryParam(url, "prompt", &value)) ||
      !HasNonWhitespace(value)) {
    return false;
  }

  if (prompt) {
    *prompt = std::move(value);
  }
  return true;
}

bool IsChatgptAutosubmitQueryParam(std::string_view encoded_name) {
  const std::string name = UrlDecodeQueryComponent(encoded_name);
  return name == "vimbrowser_prompt" || name == "vimbrowser_autosend" ||
         name == "q" || name == "prompt";
}

bool StripChatgptAutosubmitQueryParams(std::string_view url,
                                       std::string* stripped_url) {
  const size_t question = url.find('?');
  if (question == std::string_view::npos) {
    return false;
  }
  const size_t fragment = url.find('#', question + 1);
  const std::string_view query =
      fragment == std::string_view::npos
          ? url.substr(question + 1)
          : url.substr(question + 1, fragment - question - 1);

  bool removed = false;
  std::string kept_query;
  size_t pos = 0;
  while (pos <= query.size()) {
    const size_t amp = query.find('&', pos);
    const std::string_view part =
        amp == std::string_view::npos ? query.substr(pos)
                                      : query.substr(pos, amp - pos);
    const size_t equals = part.find('=');
    const std::string_view name =
        equals == std::string_view::npos ? part : part.substr(0, equals);

    if (IsChatgptAutosubmitQueryParam(name)) {
      removed = true;
    } else if (!part.empty()) {
      if (!kept_query.empty()) {
        kept_query.push_back('&');
      }
      kept_query.append(part.data(), part.size());
    }

    if (amp == std::string_view::npos) {
      break;
    }
    pos = amp + 1;
  }

  if (!removed) {
    return false;
  }

  std::string out(url.substr(0, question));
  if (!kept_query.empty()) {
    out.push_back('?');
    out += kept_query;
  }
  if (fragment != std::string_view::npos) {
    out.append(url.substr(fragment));
  }
  if (stripped_url) {
    *stripped_url = std::move(out);
  }
  return true;
}

bool IsTrackerHost(std::string_view host) {
  // Native lightweight request blocking. Monkeytype is a good stress test here:
  // accepting the consent dialog otherwise pulls in multiple ad auctions,
  // identity-sync pixels, and analytics loops which run long main-thread tasks
  // while the user is typing. Keep this list focused on ad/auction/analytics
  // infrastructure and leave first-party app/CDN/auth resources alone.
  static constexpr std::string_view kBlockedDomains[] = {
      "2mdn.net",
      "3lift.com",
      "ad-delivery.net",
      "ad.doubleclick.net",
      "adnxs.com",
      "adservice.google.com",
      "adservice.google.com.pa",
      "adsrvr.org",
      "amazon-adsystem.com",
      "analytics.yahoo.com",
      "api.btloader.com",
      "btloader.com",
      "ccgateway.net",
      "cdn.intergi.com",
      "cdn.intergient.com",
      "criteo.com",
      "crwdcntrl.net",
      "dns-finder.com",
      "doubleclick.net",
      "everesttech.net",
      "eyeota.net",
      "fixedfold.com",
      "google-analytics.com",
      "googlesyndication.com",
      "googletagmanager.com",
      "id5-sync.com",
      "intergi.com",
      "intergient.com",
      "lijit.com",
      "pubmatic.com",
      "rubiconproject.com",
      "scorecardresearch.com",
      "tapad.com",
  };
  constexpr size_t kBlockedDomainCount =
      sizeof(kBlockedDomains) / sizeof(kBlockedDomains[0]);
  auto is_blocked_domain = [](std::string_view domain) {
    return std::binary_search(kBlockedDomains,
                              kBlockedDomains + kBlockedDomainCount,
                              domain);
  };

  if (is_blocked_domain(host)) {
    return true;
  }

  size_t dot = host.find('.');
  while (dot != std::string_view::npos) {
    const std::string_view parent = host.substr(dot + 1);
    if (!parent.empty() && is_blocked_domain(parent)) {
      return true;
    }
    dot = host.find('.', dot + 1);
  }
  return false;
}

bool ShouldBlockRequest(CefRefPtr<CefRequest> request) {
  if (!NativeContentBlockingEnabled()) {
    return false;
  }
  if (!request) {
    return false;
  }
  const cef_resource_type_t resource_type = request->GetResourceType();
  if (resource_type == RT_MAIN_FRAME) {
    return false;
  }
  if (resource_type == RT_STYLESHEET || resource_type == RT_FONT_RESOURCE) {
    return false;
  }
  const std::string host = HostFromUrl(request->GetURL().ToString());
  return !host.empty() && IsTrackerHost(host);
}

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

std::string BuildChatgptAutoSubmitScript(std::string_view prompt,
                                         std::string_view key) {
  std::ostringstream script;
  script << "(()=>{\n"
         << "'use strict';\n"
         << "const prompt=\"" << JsonEscape(prompt) << "\";\n"
         << "const key=\"" << JsonEscape(key) << "\";\n"
         << R"JS(
const doneKey='vimbrowser:chatgpt-autosubmit:done:'+key;
if(!prompt||sessionStorage.getItem(doneKey)==='1'||window.__vimbrowserChatgptAutoSubmitKey===key)return;
window.__vimbrowserChatgptAutoSubmitKey=key;
let attempts=0;
let composerFilled=false;
function visible(el){
  if(!el||!el.isConnected)return false;
  const style=getComputedStyle(el);
  if(style.display==='none'||style.visibility==='hidden')return false;
  const rect=el.getBoundingClientRect();
  return rect.width>0&&rect.height>0;
}
function disabled(el){return !el||el.disabled||el.getAttribute('aria-disabled')==='true';}
function firstVisible(selectors,root){
  root=root||document;
  for(const selector of selectors){
    for(const el of root.querySelectorAll(selector)){
      if(visible(el)&&!disabled(el)&&!el.readOnly)return el;
    }
  }
  return null;
}
function findComposer(){
  return firstVisible([
    'textarea#prompt-textarea',
    'textarea[data-testid="prompt-textarea"]',
    'textarea[placeholder*="Message"]',
    '#prompt-textarea[contenteditable="true"]',
    '[data-testid="prompt-textarea"][contenteditable="true"]',
    '[contenteditable="true"][aria-label*="Message"]',
    '[contenteditable="true"][role="textbox"]'
  ],document);
}
function readComposer(el){
  if(!el)return '';
  if('value' in el)return el.value||'';
  return el.innerText||el.textContent||'';
}
function fireInput(el,data,inputType){
  let event;
  try{event=new InputEvent('input',{bubbles:true,composed:true,inputType:inputType||'insertText',data});}
  catch(_){event=new Event('input',{bubbles:true});}
  el.dispatchEvent(event);
  el.dispatchEvent(new Event('change',{bubbles:true}));
}
function setNativeValue(el,value){
  const proto=Object.getPrototypeOf(el);
  const descriptor=Object.getOwnPropertyDescriptor(proto,'value')||
    Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype,'value')||
    Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value');
  if(descriptor&&descriptor.set)descriptor.set.call(el,value);
  else el.value=value;
}
function fillComposer(el,value){
  el.focus({preventScroll:false});
  if('value' in el){
    setNativeValue(el,value);
    fireInput(el,value);
    return;
  }
  const selection=getSelection();
  const range=document.createRange();
  range.selectNodeContents(el);
  selection.removeAllRanges();
  selection.addRange(range);
  let inserted=false;
  try{inserted=document.execCommand('insertText',false,value);}catch(_){}
  if(!inserted){
    while(el.firstChild)el.removeChild(el.firstChild);
    for(const line of value.split('\n')){
      const p=document.createElement('p');
      if(line)p.textContent=line;
      else p.appendChild(document.createElement('br'));
      el.appendChild(p);
    }
  }
  fireInput(el,value);
}
function clearComposer(el){
  if(!el)return;
  el.focus({preventScroll:true});
  if('value' in el){
    setNativeValue(el,'');
    fireInput(el,'','deleteContentBackward');
    return;
  }
  const selection=getSelection();
  const range=document.createRange();
  range.selectNodeContents(el);
  selection.removeAllRanges();
  selection.addRange(range);
  let deleted=false;
  try{deleted=document.execCommand('delete',false,null);}catch(_){ }
  if(!deleted){
    while(el.firstChild)el.removeChild(el.firstChild);
    const p=document.createElement('p');
    p.appendChild(document.createElement('br'));
    el.appendChild(p);
  }
  fireInput(el,'','deleteContentBackward');
}
function uniqueRoots(composer){
  const roots=[];
  const add=(el)=>{if(el&&!roots.includes(el))roots.push(el);};
  add(composer&&composer.closest('form'));
  add(composer&&composer.closest('[data-testid*="composer"]'));
  let node=composer;
  for(let i=0;node&&i<8;++i,node=node.parentElement)add(node);
  add(document);
  return roots;
}
function findSendButton(composer){
  const selectors=[
    'button[data-testid="send-button"]',
    'button[data-testid="composer-submit-button"]',
    'button[aria-label="Send prompt"]',
    'button[aria-label="Send message"]',
    'button[aria-label*="Send"]',
    'button[type="submit"]'
  ];
  for(const root of uniqueRoots(composer)){
    const button=firstVisible(selectors,root);
    if(button)return button;
  }
  for(const button of document.querySelectorAll('button')){
    const label=(button.getAttribute('aria-label')||button.title||button.textContent||'').toLowerCase();
    if(label.includes('send')&&visible(button)&&!disabled(button))return button;
  }
  return null;
}
function scrubUrl(){
  try{
    const url=new URL(location.href);
    if(!url.searchParams.has('vimbrowser_autosend'))return;
    url.searchParams.delete('q');
    url.searchParams.delete('prompt');
    url.searchParams.delete('vimbrowser_prompt');
    url.searchParams.delete('vimbrowser_autosend');
    history.replaceState(history.state,document.title,url.pathname+url.search+url.hash);
  }catch(_){}
}
let postSubmitScrubberStarted=false;
function startPostSubmitScrubber(){
  if(postSubmitScrubberStarted)return;
  postSubmitScrubberStarted=true;
  const expected=prompt.trim();
  const deadline=Date.now()+15000;
  let timer=0;
  const clean=()=>{
    const composer=findComposer();
    if(composer&&readComposer(composer).trim()===expected)clearComposer(composer);
    if(Date.now()>=deadline&&timer)clearInterval(timer);
  };
  setTimeout(clean,500);
  timer=setInterval(clean,500);
}
function markDone(){
  sessionStorage.setItem(doneKey,'1');
  scrubUrl();
  startPostSubmitScrubber();
}
function attempt(){
  ++attempts;
  const composer=findComposer();
  if(!composer)return false;
  if(!composerFilled||readComposer(composer).trim()!==prompt.trim()){
    fillComposer(composer,prompt);
    composerFilled=true;
  }
  const button=findSendButton(composer);
  if(button){
    button.click();
    markDone();
    return true;
  }
  const form=composer.closest('form');
  if(form&&attempts>8){
    try{if(typeof form.requestSubmit==='function'){form.requestSubmit();markDone();return true;}}catch(_){}
    try{if(form.dispatchEvent(new Event('submit',{bubbles:true,cancelable:true}))){markDone();return true;}}catch(_){}
  }
  return false;
}
const timer=setInterval(()=>{if(attempt()||attempts>=360)clearInterval(timer);},250);
setTimeout(()=>{if(attempt())clearInterval(timer);},50);
})();
)JS";
  return script.str();
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

bool BrowserClient::OnFileDialog(
    CefRefPtr<CefBrowser> browser,
    FileDialogMode mode,
    const CefString& title,
    const CefString& default_file_path,
    const std::vector<CefString>& accept_filters,
    const std::vector<CefString>& accept_extensions,
    const std::vector<CefString>& accept_descriptions,
    CefRefPtr<CefFileDialogCallback> callback) {
  uint64_t activation_nonce_high = 0;
  uint64_t activation_nonce_low = 0;
  const bool has_activation_nonce =
      browser && vimbrowser_get_current_file_dialog_activation_nonce(
                     browser->GetIdentifier(), &activation_nonce_high,
                     &activation_nonce_low);
  return owner_ && owner_->OnClientFileDialog(
                       this, browser, mode, accept_filters, accept_extensions,
                       has_activation_nonce, activation_nonce_high,
                       activation_nonce_low,
                       std::move(callback));
}

void BrowserClient::DetachOwner() {
  owner_ = nullptr;
}

void BrowserClient::OnLoadStart(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                TransitionType transition_type) {
  if (frame && frame->IsMain()) {
#if defined(__APPLE__)
    fps_has_sample_.store(false, std::memory_order_relaxed);
    fps_.store(0.0, std::memory_order_relaxed);
    refresh_rate_.store(0.0, std::memory_order_relaxed);
#endif
    const std::string url = frame->GetURL().ToString();
    std::string prompt;
    if (ExtractChatgptAutosubmitPrompt(url, &prompt)) {
      pending_chatgpt_autosubmit_prompt_ = std::move(prompt);
      pending_chatgpt_autosubmit_key_ =
          "chatgpt:" + std::to_string(++chatgpt_autosubmit_sequence_);
    } else if (!IsChatgptUrl(url) && !IsChatgptAuthUrl(url)) {
      pending_chatgpt_autosubmit_prompt_.clear();
      pending_chatgpt_autosubmit_key_.clear();
    }
    if (owner_) {
      owner_->OnClientLoadStart(this, url);
    }
  }
}

void BrowserClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                         bool isLoading,
                                         bool canGoBack,
                                         bool canGoForward) {
  if (owner_) {
    owner_->OnClientLoadingStateChange(this, isLoading, canGoBack,
                                       canGoForward);
  }
}

void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              int httpStatusCode) {
  if (frame && frame->IsMain()) {
#if defined(__APPLE__)
    frame->ExecuteJavaScript(mac::kFpsMonitorScript, frame->GetURL(), 0);
#endif
    if (browser && !pending_chatgpt_autosubmit_prompt_.empty() &&
        !pending_chatgpt_autosubmit_key_.empty() &&
        IsChatgptUrl(frame->GetURL().ToString())) {
      frame->ExecuteJavaScript(
          BuildChatgptAutoSubmitScript(pending_chatgpt_autosubmit_prompt_,
                                       pending_chatgpt_autosubmit_key_),
          frame->GetURL(), 0);
    }
  }
}

void BrowserClient::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    const CefString& url) {
  if (owner_ && frame && frame->IsMain()) {
    owner_->OnClientAddressChange(this, url.ToString());
  }
}

void BrowserClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                  const CefString& title) {
  if (owner_) {
    owner_->OnClientTitleChange(this, title.ToString());
  }
}

#if CEF_API_ADDED(13700)
bool BrowserClient::GetRootWindowScreenRect(CefRefPtr<CefBrowser> browser,
                                            CefRect& rect) {
  return owner_ && owner_->GetRootWindowScreenRectForClient(this, rect);
}
#endif

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

  settings.tab_to_links = STATE_ENABLED;
  ApplyBrowserFontSettings(settings);

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
#if defined(__APPLE__)
  constexpr std::string_view kFpsPrefix = "__vimbrowser_mac_fps__";
  if (text.rfind(kFpsPrefix, 0) == 0) {
    char* end = nullptr;
    const char* start = text.c_str() + kFpsPrefix.size();
    const double sample = std::strtod(start, &end);
    if (end != start && std::isfinite(sample) && sample > 0.0) {
      fps_.store(sample, std::memory_order_relaxed);
      if (*end == ',') {
        char* refresh_end = nullptr;
        const double refresh = std::strtod(end + 1, &refresh_end);
        if (refresh_end != end + 1 && std::isfinite(refresh) && refresh > 0.0) {
          refresh_rate_.store(refresh, std::memory_order_relaxed);
        }
      }
      fps_has_sample_.store(true, std::memory_order_relaxed);
    }
    return true;
  }
#endif
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
          bool is_pdf_viewport = false;
          if (*y_end == ',') {
            char* page_end = nullptr;
            const long page = std::strtol(y_end + 1, &page_end, 10);
            is_page_scroller = page_end != y_end + 1 && page != 0;
            if (page_end && *page_end == ',') {
              char* pdf_end = nullptr;
              const long pdf = std::strtol(page_end + 1, &pdf_end, 10);
              is_pdf_viewport = pdf_end != page_end + 1 && pdf != 0;
            }
          }
          owner_->OnNativeHintScrollTarget(this, static_cast<int>(x),
                                          static_cast<int>(y),
                                          is_page_scroller,
                                          is_pdf_viewport);
        }
      }
    }
    return true;
  }
  if (text == "__vimbrowser_native_hint_focused_editable__") {
    if (owner_) {
      owner_->OnNativeHintFocusedEditable(this);
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

bool BrowserClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request,
                                   bool user_gesture,
                                   bool is_redirect) {
  (void)browser;
  (void)user_gesture;
  (void)is_redirect;

  if (!frame || !frame->IsMain() || !request) {
    return false;
  }

  const std::string url = request->GetURL().ToString();
  std::string prompt;
  if (!ExtractChatgptAutosubmitPrompt(url, &prompt)) {
    return false;
  }

  pending_chatgpt_autosubmit_prompt_ = std::move(prompt);
  pending_chatgpt_autosubmit_key_ =
      "chatgpt:" + std::to_string(++chatgpt_autosubmit_sequence_);

  std::string stripped_url;
  if (!StripChatgptAutosubmitQueryParams(url, &stripped_url) ||
      stripped_url == url) {
    return false;
  }

  frame->LoadURL(stripped_url);
  return true;
}

bool BrowserClient::OnRequestMediaAccessPermission(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& requesting_origin,
    uint32_t requested_permissions,
    CefRefPtr<CefMediaAccessCallback> callback) {
  CefRefPtr<BrowserClient> keep_alive(this);
  if (owner_) {
    return owner_->OnClientMediaAccessRequest(
        this, browser, frame, requesting_origin, requested_permissions, callback);
  }

  if (callback) {
    callback->Continue(CEF_MEDIA_PERMISSION_NONE);
  }
  return true;
}

#if defined(__APPLE__)
void BrowserClient::OnAudioStreamStarted(CefRefPtr<CefBrowser>,
                                         const CefAudioParameters&,
                                         int) {
  audible_.store(true, std::memory_order_relaxed);
}

void BrowserClient::OnAudioStreamPacket(CefRefPtr<CefBrowser>,
                                        const float**,
                                        int,
                                        int64_t) {}

void BrowserClient::OnAudioStreamStopped(CefRefPtr<CefBrowser>) {
  audible_.store(false, std::memory_order_relaxed);
}

void BrowserClient::OnAudioStreamError(CefRefPtr<CefBrowser>,
                                       const CefString&) {
  audible_.store(false, std::memory_order_relaxed);
}
#endif

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

  // Ctrl+Shift+I opens/focuses the docked DevTools surface just like Chromium.
  if (ctrl && shift && event.windows_key_code == 'I') {
    ShowDevTools();
    return true;
  }

  return false;
}

void BrowserClient::OnBeforeContextMenu(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params,
    CefRefPtr<CefMenuModel> model) {
  if (!owner_ || !model) {
    return;
  }

  // Replace Chromium/CEF's default Views MenuRunner popup with vimbrowser's
  // own chrome overlay. Keep a tiny placeholder model so CEF still calls
  // RunContextMenu(), but never let the default native menu display.
  model->Clear();
  model->AddItem(MENU_ID_USER_FIRST, "vimbrowser native context menu");
}

bool BrowserClient::RunContextMenu(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params,
    CefRefPtr<CefMenuModel> model,
    CefRefPtr<CefRunContextMenuCallback> callback) {
  if (!owner_) {
    if (callback) {
      callback->Cancel();
    }
    return true;
  }

  return owner_->RunNativeContextMenu(this, browser, frame, params, callback);
}

bool BrowserClient::OnContextMenuCommand(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params,
    int command_id,
    cef_event_flags_t event_flags) {
  return owner_ && owner_->OnNativeContextMenuCommand(
                       this, browser, frame, params, command_id, event_flags);
}

void BrowserClient::OnContextMenuDismissed(CefRefPtr<CefBrowser> browser,
                                           CefRefPtr<CefFrame> frame) {
  if (owner_) {
    owner_->OnNativeContextMenuDismissed(this);
  }
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

  if (!NativeNetworkCaptureEnabled()) {
    return ShouldBlockRequest(request) ? this : nullptr;
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
  if (ShouldBlockRequest(request)) {
    return RV_CANCEL;
  }
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

  if (owner_) {
    owner_->ShowDevToolsForClient(this);
    return;
  }

  CefWindowInfo window_info;
  CefBrowserSettings settings;
  browser_->GetHost()->ShowDevTools(window_info, this, settings, CefPoint());
}

double BrowserClient::current_fps() const {
#if defined(__APPLE__)
  return fps_.load(std::memory_order_relaxed);
#else
  return browser_ ? vimbrowser_get_browser_fps(browser_->GetIdentifier()) : 0.0;
#endif
}

bool BrowserClient::fps_has_sample() const {
#if defined(__APPLE__)
  return fps_has_sample_.load(std::memory_order_relaxed);
#else
  return browser_ && vimbrowser_browser_has_fps_sample(browser_->GetIdentifier());
#endif
}

double BrowserClient::compositor_refresh_rate() const {
#if defined(__APPLE__)
  return refresh_rate_.load(std::memory_order_relaxed);
#else
  return browser_ ? vimbrowser_get_browser_refresh_rate(browser_->GetIdentifier())
                  : 0.0;
#endif
}

bool BrowserClient::is_currently_audible() const {
#if defined(__APPLE__)
  return audible_.load(std::memory_order_relaxed);
#else
  return browser_ &&
         vimbrowser_browser_is_currently_audible(browser_->GetIdentifier());
#endif
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
