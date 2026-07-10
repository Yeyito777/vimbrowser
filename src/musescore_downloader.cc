#include "musescore_downloader.h"

#include <cairo/cairo-pdf.h>
#include <cairo/cairo.h>
#include <curl/curl.h>
#include <librsvg/rsvg.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace vimbrowser {
namespace {

constexpr size_t kMaximumPageBytes = 64 * 1024 * 1024;
constexpr size_t kMaximumScoreBytes = 512 * 1024 * 1024;
constexpr double kPdfPointsPerCssPixel = 72.0 / 96.0;

const std::string kMuseScoreMetadataScript = R"JS((function(){
function getTitle(){
  var og=document.querySelector('meta[property="og:title"]');
  if(og&&og.content) return og.content.trim();
  var ld=Array.from(document.querySelectorAll('script[type="application/ld+json"]')).map(function(s){try{return JSON.parse(s.textContent)}catch(e){return null}}).find(function(o){return o&&o['@type']==='MusicComposition'});
  if(ld&&ld.name) return String(ld.name).trim();
  return document.title.replace(/\s*Sheet Music.*$/,'').trim();
}
function getScoreId(){
  var meta=document.querySelector("meta[property='al:ios:url']")
    || document.querySelector("meta[property='al:android:url']")
    || document.querySelector("meta[property='og:url']");
  var txt=(meta&&meta.content)||'';
  var m=txt.match(/(\d+)(?:\D*)$/);
  if(!m) throw new Error('could not determine score id');
  return +m[1];
}
function getPageCount(){
  var body=document.body ? document.body.outerHTML : '';
  var m=body.match(/\d+ of (\d+) pages/);
  if(m) return +m[1];
  m=body.match(/Pages<\/h3><\/th><td><div[\w =\"]+>(\d+)</);
  if(m) return +m[1];
  var cls=document.querySelector('#jmuse-scroller-component')?.firstChild?.classList?.[0];
  if(cls){
    var n=document.querySelectorAll('.'+cls).length;
    if(n) return n;
  }
  var img=document.querySelector('img[src*=score_]');
  var className=img&&img.parentElement&&img.parentElement.className;
  if(className){
    var count=document.getElementsByClassName(className).length;
    if(count) return count;
  }
  throw new Error('could not determine page count');
}
function getBundleUrl(){
  var urls=Array.from(document.querySelectorAll('script[src],link[href]')).map(function(e){return e.src||e.href});
  var url=urls.find(function(u){return /https:\/\/musescore\.com\/static\/public\/build\/[\w\/]+\/\d+\/\d+\.\w+\.js/.test(u)});
  if(!url) throw new Error('could not find MuseScore bundle url');
  return url;
}
function buildTokenGenerator(){
  var url=getBundleUrl();
  var x=new XMLHttpRequest();
  x.open('GET', url, false);
  x.send(null);
  if(x.status !== 200) throw new Error('failed to fetch MuseScore bundle: '+x.status);
  var script=x.responseText;
  var randomToken=(script.match(/"([\W\w]{1,50})"\)\.substr\(0, *4\)/)||[])[1];
  var parts=script.split(/, *(\d+): *(?:function)*\([\w,]{1,8}\)(?: *=> *|)\{/);
  var functionNumber=null;
  for(var i=0;i<parts.length;i++){
    if(parts[i].includes('_digestsize') && parts[i].includes('_blocksize')){
      functionNumber=parts[i-1];
      break;
    }
  }
  if(!randomToken || !functionNumber) throw new Error('failed to reconstruct MuseScore token generator');
  script=script.replace(/\(self\.[^}]*(?=\{(\d+):)/,'(function (modules) { var installedModules = {}; function __webpack_require__(moduleId) { if (installedModules[moduleId]) { return installedModules[moduleId].exports; } var module = installedModules[moduleId] = { i: moduleId, l: false, exports: {} }; modules[moduleId].call(module.exports, module, module.exports, __webpack_require__); module.l = true; return module.exports; } __webpack_require__.m = modules; __webpack_require__.c = installedModules; return __webpack_require__(__webpack_require__.s = '+functionNumber+'); })(');
  script=script.replace(/}}]\)/,'}})');
  script=script.replace(/_digestsize=(\d+),\w+\.exports=function\(/, function(match, a){ return '_digestsize='+a+',window.generateToken=function(' });
  new Function(script)();
  return function(id, type, index){
    return window.generateToken(String(id)+type+String(index)+randomToken).substring(0, 4);
  };
}
function getSignedImageUrl(id, index, getToken){
  var token=getToken(id, 'img', index);
  var x=new XMLHttpRequest();
  x.open('GET', '/api/jmuse?id='+id+'&index='+index+'&type=img', false);
  x.setRequestHeader('Authorization', token);
  x.send(null);
  if(x.status !== 200) throw new Error('MuseScore image API returned '+x.status+' for page '+(index+1));
  var data=JSON.parse(x.responseText);
  if(!data || !data.info || !data.info.url) throw new Error('missing signed url for page '+(index+1));
  return data.info.url;
}
var scoreId=getScoreId();
var pageCount=getPageCount();
var getToken=buildTokenGenerator();
var urls=[];
for(var i=0;i<pageCount;i++) urls.push(getSignedImageUrl(scoreId, i, getToken));
return JSON.stringify({scoreId: scoreId, title: getTitle(), pageCount: pageCount, urls: urls});
})())JS";

struct DownloadedPage {
  std::string url;
  std::string content_type;
  std::vector<unsigned char> bytes;
};

struct CurlWriteState {
  std::vector<unsigned char>* bytes = nullptr;
  bool too_large = false;
};

struct PngReadState {
  const std::vector<unsigned char>* bytes = nullptr;
  size_t offset = 0;
};

struct GErrorDeleter {
  void operator()(GError* error) const {
    if (error) {
      g_error_free(error);
    }
  }
};

struct RsvgHandleDeleter {
  void operator()(RsvgHandle* handle) const {
    if (handle) {
      g_object_unref(handle);
    }
  }
};

struct CairoSurfaceDeleter {
  void operator()(cairo_surface_t* surface) const {
    if (surface) {
      cairo_surface_destroy(surface);
    }
  }
};

struct CairoDeleter {
  void operator()(cairo_t* context) const {
    if (context) {
      cairo_destroy(context);
    }
  }
};

using UniqueGError = std::unique_ptr<GError, GErrorDeleter>;
using UniqueRsvgHandle = std::unique_ptr<RsvgHandle, RsvgHandleDeleter>;
using UniqueCairoSurface =
    std::unique_ptr<cairo_surface_t, CairoSurfaceDeleter>;
using UniqueCairo = std::unique_ptr<cairo_t, CairoDeleter>;

size_t WriteCurlData(char* data, size_t size, size_t count, void* userdata) {
  auto* state = static_cast<CurlWriteState*>(userdata);
  if (!state || !state->bytes || size == 0 || count == 0) {
    return 0;
  }
  if (count > std::numeric_limits<size_t>::max() / size) {
    state->too_large = true;
    return 0;
  }
  const size_t length = size * count;
  if (length > kMaximumPageBytes ||
      state->bytes->size() > kMaximumPageBytes - length) {
    state->too_large = true;
    return 0;
  }
  const auto* begin = reinterpret_cast<const unsigned char*>(data);
  state->bytes->insert(state->bytes->end(), begin, begin + length);
  return length;
}

cairo_status_t ReadPngData(void* userdata,
                           unsigned char* output,
                           unsigned int length) {
  auto* state = static_cast<PngReadState*>(userdata);
  if (!state || !state->bytes ||
      state->offset > state->bytes->size() ||
      length > state->bytes->size() - state->offset) {
    return CAIRO_STATUS_READ_ERROR;
  }
  std::memcpy(output, state->bytes->data() + state->offset, length);
  state->offset += length;
  return CAIRO_STATUS_SUCCESS;
}

std::string LowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string SanitizeFilename(const std::string& title) {
  std::string out;
  out.reserve(title.size());
  bool previous_space = false;
  for (unsigned char c : title) {
    const bool forbidden = c < 0x20 || c == '<' || c == '>' || c == ':' ||
                           c == '"' || c == '/' || c == '\\' || c == '|' ||
                           c == '?' || c == '*';
    if (forbidden) {
      continue;
    }
    if (std::isspace(c)) {
      if (!out.empty() && !previous_space) {
        out.push_back(' ');
      }
      previous_space = true;
      continue;
    }
    out.push_back(static_cast<char>(c));
    previous_space = false;
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
    out.pop_back();
  }
  return out.empty() ? "musescore-score" : out;
}

std::filesystem::path UniqueOutputPath(const std::filesystem::path& directory,
                                       const std::string& title) {
  const std::string stem = SanitizeFilename(title);
  std::filesystem::path candidate = directory / (stem + ".pdf");
  std::error_code error;
  if (!std::filesystem::exists(candidate, error)) {
    return candidate;
  }
  for (size_t i = 1;; ++i) {
    candidate = directory / (stem + " (" + std::to_string(i) + ").pdf");
    error.clear();
    if (!std::filesystem::exists(candidate, error)) {
      return candidate;
    }
  }
}

bool EnsureCurl(std::string* error) {
  static std::once_flag once;
  static CURLcode initialization = CURLE_FAILED_INIT;
  std::call_once(once, [] { initialization = curl_global_init(CURL_GLOBAL_DEFAULT); });
  if (initialization == CURLE_OK) {
    return true;
  }
  if (error) {
    *error = std::string("failed to initialize libcurl: ") +
             curl_easy_strerror(initialization);
  }
  return false;
}

bool DownloadPage(const std::string& url,
                  DownloadedPage* page,
                  std::string* error) {
  if (!page || !error || !EnsureCurl(error)) {
    return false;
  }
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
                                                          curl_easy_cleanup);
  if (!curl) {
    *error = "failed to create a native HTTP request";
    return false;
  }

  page->url = url;
  CurlWriteState state{&page->bytes, false};
  char curl_error[CURL_ERROR_SIZE] = {};
  curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 10L);
  curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 20L);
  curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 90L);
  curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl.get(), CURLOPT_USERAGENT,
                   "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                   "(KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36");
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCurlData);
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &state);
  curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, curl_error);

  const CURLcode code = curl_easy_perform(curl.get());
  if (code != CURLE_OK) {
    *error = state.too_large
                 ? "a score page exceeded the 64 MiB safety limit"
                 : std::string("page download failed: ") +
                       (curl_error[0] ? curl_error : curl_easy_strerror(code));
    return false;
  }

  long status = 0;
  curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
  if (status < 200 || status >= 300) {
    *error = "MuseScore page server returned HTTP " + std::to_string(status);
    return false;
  }
  char* content_type = nullptr;
  curl_easy_getinfo(curl.get(), CURLINFO_CONTENT_TYPE, &content_type);
  if (content_type) {
    page->content_type = LowerAscii(content_type);
  }
  if (page->bytes.empty()) {
    *error = "MuseScore returned an empty score page";
    return false;
  }
  return true;
}

bool LooksLikeSvg(const DownloadedPage& page) {
  if (page.content_type.find("svg") != std::string::npos) {
    return true;
  }
  const size_t inspect = std::min<size_t>(page.bytes.size(), 2048);
  std::string prefix(reinterpret_cast<const char*>(page.bytes.data()), inspect);
  return LowerAscii(prefix).find("<svg") != std::string::npos;
}

bool LooksLikePng(const DownloadedPage& page) {
  static constexpr unsigned char kPngSignature[] = {
      0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  return page.content_type.find("png") != std::string::npos ||
         (page.bytes.size() >= sizeof(kPngSignature) &&
          std::equal(std::begin(kPngSignature), std::end(kPngSignature),
                     page.bytes.begin()));
}

bool ValidPageDimensions(double width, double height) {
  return std::isfinite(width) && std::isfinite(height) && width > 0.0 &&
         height > 0.0 && width <= 100000.0 && height <= 100000.0;
}

bool PreparePdfPage(cairo_surface_t** pdf,
                    const std::filesystem::path& path,
                    double width_pixels,
                    double height_pixels,
                    std::string* error) {
  if (!ValidPageDimensions(width_pixels, height_pixels)) {
    *error = "score page has invalid dimensions";
    return false;
  }
  const double width_points = width_pixels * kPdfPointsPerCssPixel;
  const double height_points = height_pixels * kPdfPointsPerCssPixel;
  if (!*pdf) {
    *pdf = cairo_pdf_surface_create(path.c_str(), width_points, height_points);
  } else {
    cairo_pdf_surface_set_size(*pdf, width_points, height_points);
  }
  const cairo_status_t status = cairo_surface_status(*pdf);
  if (status != CAIRO_STATUS_SUCCESS) {
    *error = std::string("failed to create PDF: ") + cairo_status_to_string(status);
    return false;
  }
  return true;
}

void PaintWhiteBackground(cairo_t* context,
                          double width_pixels,
                          double height_pixels) {
  cairo_save(context);
  cairo_set_source_rgb(context, 1.0, 1.0, 1.0);
  cairo_rectangle(context, 0.0, 0.0, width_pixels, height_pixels);
  cairo_fill(context);
  cairo_restore(context);
}

bool RenderSvgPage(const DownloadedPage& page,
                   cairo_surface_t** pdf,
                   const std::filesystem::path& path,
                   std::string* error) {
  GError* raw_error = nullptr;
  UniqueRsvgHandle handle(rsvg_handle_new_from_data(
      page.bytes.data(), page.bytes.size(), &raw_error));
  UniqueGError parse_error(raw_error);
  if (!handle) {
    *error = std::string("failed to decode SVG score page") +
             (parse_error ? ": " + std::string(parse_error->message) : "");
    return false;
  }

  double width = 0.0;
  double height = 0.0;
  if (!rsvg_handle_get_intrinsic_size_in_pixels(handle.get(), &width, &height)) {
    gboolean has_width = FALSE;
    gboolean has_height = FALSE;
    gboolean has_viewbox = FALSE;
    RsvgLength intrinsic_width{};
    RsvgLength intrinsic_height{};
    RsvgRectangle viewbox{};
    rsvg_handle_get_intrinsic_dimensions(
        handle.get(), &has_width, &intrinsic_width, &has_height,
        &intrinsic_height, &has_viewbox, &viewbox);
    if (has_viewbox) {
      width = viewbox.width;
      height = viewbox.height;
    }
  }
  if (!PreparePdfPage(pdf, path, width, height, error)) {
    return false;
  }

  UniqueCairo context(cairo_create(*pdf));
  if (!context || cairo_status(context.get()) != CAIRO_STATUS_SUCCESS) {
    *error = "failed to initialize the PDF renderer";
    return false;
  }
  cairo_scale(context.get(), kPdfPointsPerCssPixel, kPdfPointsPerCssPixel);
  PaintWhiteBackground(context.get(), width, height);
  RsvgRectangle viewport{0.0, 0.0, width, height};
  raw_error = nullptr;
  const bool rendered = rsvg_handle_render_document(
      handle.get(), context.get(), &viewport, &raw_error);
  UniqueGError render_error(raw_error);
  if (!rendered) {
    *error = std::string("failed to render SVG score page") +
             (render_error ? ": " + std::string(render_error->message) : "");
    return false;
  }
  cairo_show_page(context.get());
  if (cairo_status(context.get()) != CAIRO_STATUS_SUCCESS) {
    *error = std::string("failed to write SVG score page: ") +
             cairo_status_to_string(cairo_status(context.get()));
    return false;
  }
  return true;
}

bool RenderPngPage(const DownloadedPage& page,
                   cairo_surface_t** pdf,
                   const std::filesystem::path& path,
                   std::string* error) {
  PngReadState read_state{&page.bytes, 0};
  UniqueCairoSurface image(
      cairo_image_surface_create_from_png_stream(ReadPngData, &read_state));
  const cairo_status_t image_status = cairo_surface_status(image.get());
  if (image_status != CAIRO_STATUS_SUCCESS) {
    *error = std::string("failed to decode PNG score page: ") +
             cairo_status_to_string(image_status);
    return false;
  }
  const double width = cairo_image_surface_get_width(image.get());
  const double height = cairo_image_surface_get_height(image.get());
  if (!PreparePdfPage(pdf, path, width, height, error)) {
    return false;
  }

  UniqueCairo context(cairo_create(*pdf));
  if (!context || cairo_status(context.get()) != CAIRO_STATUS_SUCCESS) {
    *error = "failed to initialize the PDF renderer";
    return false;
  }
  cairo_scale(context.get(), kPdfPointsPerCssPixel, kPdfPointsPerCssPixel);
  PaintWhiteBackground(context.get(), width, height);
  cairo_set_source_surface(context.get(), image.get(), 0.0, 0.0);
  cairo_paint(context.get());
  cairo_show_page(context.get());
  if (cairo_status(context.get()) != CAIRO_STATUS_SUCCESS) {
    *error = std::string("failed to write PNG score page: ") +
             cairo_status_to_string(cairo_status(context.get()));
    return false;
  }
  return true;
}

bool BuildPdf(const std::vector<DownloadedPage>& pages,
              const std::filesystem::path& path,
              std::string* error) {
  cairo_surface_t* raw_pdf = nullptr;
  for (const DownloadedPage& page : pages) {
    const bool rendered = LooksLikeSvg(page)
                              ? RenderSvgPage(page, &raw_pdf, path, error)
                              : LooksLikePng(page)
                                    ? RenderPngPage(page, &raw_pdf, path, error)
                                    : false;
    if (!rendered) {
      if (error->empty()) {
        *error = "MuseScore returned an unsupported score-page image format";
      }
      if (raw_pdf) {
        cairo_surface_destroy(raw_pdf);
      }
      return false;
    }
  }
  if (!raw_pdf) {
    *error = "no score pages were found";
    return false;
  }
  cairo_surface_finish(raw_pdf);
  const cairo_status_t status = cairo_surface_status(raw_pdf);
  cairo_surface_destroy(raw_pdf);
  if (status != CAIRO_STATUS_SUCCESS) {
    *error = std::string("failed to finish PDF: ") + cairo_status_to_string(status);
    return false;
  }
  return true;
}

}  // namespace

const std::string& MuseScoreMetadataScript() {
  return kMuseScoreMetadataScript;
}

std::filesystem::path DefaultMuseScoreDownloadDirectory() {
  if (const char* configured = std::getenv("MUSESCORE_DOWNLOAD_DIR");
      configured && configured[0]) {
    return configured;
  }
  if (const char* home = std::getenv("HOME"); home && home[0]) {
    return std::filesystem::path(home) / "Desktop" / "musescore-sheets";
  }
  return std::filesystem::current_path() / "musescore-sheets";
}

MuseScorePdfResult DownloadMuseScorePdf(
    const std::string& title,
    const std::vector<std::string>& urls,
    const std::filesystem::path& download_directory,
    const MuseScoreProgressCallback& progress) {
  MuseScorePdfResult result;
  if (urls.empty()) {
    result.error = "no score pages were found";
    return result;
  }

  std::error_code filesystem_error;
  std::filesystem::create_directories(download_directory, filesystem_error);
  if (filesystem_error) {
    result.error = "could not create download directory: " +
                   filesystem_error.message();
    return result;
  }

  std::vector<DownloadedPage> pages;
  pages.reserve(urls.size());
  size_t total_bytes = 0;
  for (size_t i = 0; i < urls.size(); ++i) {
    if (progress) {
      progress("MuseScore: downloading page " + std::to_string(i + 1) + "/" +
               std::to_string(urls.size()) + "...");
    }
    DownloadedPage page;
    if (!DownloadPage(urls[i], &page, &result.error)) {
      result.error = "page " + std::to_string(i + 1) + ": " + result.error;
      return result;
    }
    if (page.bytes.size() > kMaximumScoreBytes - total_bytes) {
      result.error = "downloaded score exceeded the 512 MiB safety limit";
      return result;
    }
    total_bytes += page.bytes.size();
    pages.push_back(std::move(page));
  }

  const std::filesystem::path output =
      UniqueOutputPath(download_directory, title);
  std::filesystem::path temporary = output;
  temporary += ".part";
  std::filesystem::remove(temporary, filesystem_error);
  filesystem_error.clear();

  if (progress) {
    progress("MuseScore: building PDF...");
  }
  if (!BuildPdf(pages, temporary, &result.error)) {
    std::filesystem::remove(temporary, filesystem_error);
    return result;
  }

  std::filesystem::rename(temporary, output, filesystem_error);
  if (filesystem_error) {
    const std::string rename_error = filesystem_error.message();
    std::error_code remove_error;
    std::filesystem::remove(temporary, remove_error);
    result.error = "could not save PDF: " + rename_error;
    return result;
  }

  result.output_path = output.string();
  return result;
}

}  // namespace vimbrowser
