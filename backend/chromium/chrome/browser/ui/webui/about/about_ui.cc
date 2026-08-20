// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/about/about_ui.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/containers/to_vector.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/format_macros.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/threading/thread.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/about_flags.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/chrome_urls/chrome_urls_ui.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/branded_strings.h"
#include "chrome/grit/browser_resources.h"
#include "chrome/grit/generated_resources.h"
#include "components/grit/components_resources.h"
#include "components/strings/grit/components_locale_settings.h"
#include "components/strings/grit/components_strings.h"
#include "components/webui/about/credit_utils.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/webui_config_map.h"
#include "content/public/common/url_constants.h"
#include "net/base/filename_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/webui/web_ui_util.h"
#include "url/gurl.h"

#include "chrome/browser/ui/webui/theme_source.h"


#if BUILDFLAG(ENABLE_CEF)
#include "cef/grit/cef_resources.h"
#endif

using content::BrowserThread;

namespace {

constexpr char kCreditsJsPath[] = "credits.js";
constexpr char kCreditsCssPath[] = "credits.css";
constexpr char kStatsJsPath[] = "stats.js";
constexpr char kStringsJsPath[] = "strings.js";


}  // namespace

// Individual about handlers ---------------------------------------------------

namespace about_ui {

void AppendHeader(std::string* output, const std::string& unescaped_title) {
  output->append("<!DOCTYPE HTML>\n<html>\n<head>\n");
  output->append("<meta charset='utf-8'>\n");
  output->append("<meta name='color-scheme' content='light dark'>\n");
  if (!unescaped_title.empty()) {
    output->append("<title>");
    output->append(base::EscapeForHTML(unescaped_title));
    output->append("</title>\n");
  }
}

void AppendBody(std::string* output) {
  output->append("</head>\n<body>\n");
}

void AppendFooter(std::string* output) {
  output->append("</body>\n</html>\n");
}

}  // namespace about_ui

using about_ui::AppendBody;
using about_ui::AppendFooter;
using about_ui::AppendHeader;

namespace {

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OPENBSD)
std::string AboutLinuxProxyConfig() {
  std::string data;
  AppendHeader(&data,
               l10n_util::GetStringUTF8(IDS_ABOUT_LINUX_PROXY_CONFIG_TITLE));
  data.append("<style>body { max-width: 70ex; padding: 2ex 5ex; }</style>");
  AppendBody(&data);
  base::FilePath binary = base::CommandLine::ForCurrentProcess()->GetProgram();
  data.append(
      l10n_util::GetStringFUTF8(IDS_ABOUT_LINUX_PROXY_CONFIG_BODY,
                                l10n_util::GetStringUTF16(IDS_PRODUCT_NAME),
                                base::ASCIIToUTF16(binary.BaseName().value())));
  AppendFooter(&data);
  return data;
}
#endif

}  // namespace

AboutUIConfigBase::AboutUIConfigBase(std::string_view host)
    : DefaultWebUIConfig(content::kChromeUIScheme, host) {}

CreditsUIConfig::CreditsUIConfig()
    : AboutUIConfigBase(chrome::kChromeUICreditsHost) {}

#if BUILDFLAG(ENABLE_CEF)
ChromeUILicenseConfig::ChromeUILicenseConfig()
    : AboutUIConfigBase(chrome::kChromeUILicenseHost) {}
#endif

TermsUIConfig::TermsUIConfig()
    : AboutUIConfigBase(chrome::kChromeUITermsHost) {}

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OPENBSD)
LinuxProxyConfigUI::LinuxProxyConfigUI()
    : AboutUIConfigBase(chrome::kChromeUILinuxProxyConfigHost) {}
#endif


// AboutUIHTMLSource ----------------------------------------------------------

AboutUIHTMLSource::AboutUIHTMLSource(const std::string& source_name,
                                     Profile* profile)
    : source_name_(source_name), profile_(profile) {}

AboutUIHTMLSource::~AboutUIHTMLSource() = default;

std::string AboutUIHTMLSource::GetSource() {
  return source_name_;
}

void AboutUIHTMLSource::StartDataRequest(
    const GURL& url,
    const content::WebContents::Getter& wc_getter,
    content::URLDataSource::GotDataCallback callback) {
  // TODO(crbug.com/40050262): Simplify usages of |path| since |url| is
  // available.
  const std::string path = content::URLDataSource::URLToRequestPath(url);
  std::string response;
  // Add your data source here, in alphabetical order.
  if (source_name_ == chrome::kChromeUICreditsHost) {
    int idr = IDR_ABOUT_UI_CREDITS_HTML;
    if (path == kCreditsJsPath) {
      idr = IDR_ABOUT_UI_CREDITS_JS;
    } else if (path == kCreditsCssPath) {
      idr = IDR_ABOUT_UI_CREDITS_CSS;
    }
    if (idr == IDR_ABOUT_UI_CREDITS_HTML) {
      response = about_ui::GetCredits(true /*include_scripts*/);
    } else {
      response =
          ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(idr);
    }
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_OPENBSD)
  } else if (source_name_ == chrome::kChromeUILinuxProxyConfigHost) {
    response = AboutLinuxProxyConfig();
#endif
  } else if (source_name_ == chrome::kChromeUITermsHost) {
    response =
        ui::ResourceBundle::GetSharedInstance().LoadLocalizedResourceString(
            IDS_TERMS_HTML);
  }
#if BUILDFLAG(ENABLE_CEF)
  else if (source_name_ == chrome::kChromeUILicenseHost) {
    response =
        "<html><head><title>CEF License</title></head>"
        "<body bgcolor=\"white\"><pre>" +
        ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
            IDR_CEF_LICENSE_TXT) +
        "</pre></body></html>";
  }
#endif

  FinishDataRequest(response, std::move(callback));
}

void AboutUIHTMLSource::FinishDataRequest(
    const std::string& html,
    content::URLDataSource::GotDataCallback callback) {
  std::move(callback).Run(base::MakeRefCounted<base::RefCountedString>(html));
}

std::string AboutUIHTMLSource::GetMimeType(const GURL& url) {
  const std::string_view path = url.path().substr(1);
  if (path == kCreditsJsPath || path == kStatsJsPath ||
      path == kStringsJsPath) {
    return "application/javascript";
  }

  if (path == kCreditsCssPath) {
    return "text/css";
  }

  return "text/html";
}

std::string AboutUIHTMLSource::GetAccessControlAllowOriginForOrigin(
    const std::string& origin) {
  return content::URLDataSource::GetAccessControlAllowOriginForOrigin(origin);
}

AboutUI::AboutUI(content::WebUI* web_ui, const GURL& url)
    : WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);

  // Set up the chrome://theme/ source.
  content::URLDataSource::Add(profile, std::make_unique<ThemeSource>(profile));

  content::URLDataSource::Add(
      profile, std::make_unique<AboutUIHTMLSource>(url.GetHost(), profile));
}
