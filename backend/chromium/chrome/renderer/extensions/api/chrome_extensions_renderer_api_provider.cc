// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/extensions/api/chrome_extensions_renderer_api_provider.h"

#include <string_view>

#include "chrome/grit/renderer_resources.h"
#include "chrome/renderer/extensions/api/extension_hooks_delegate.h"
#include "chrome/renderer/extensions/api/notifications_native_handler.h"
#include "chrome/renderer/extensions/api/page_capture_custom_bindings.h"
#include "chrome/renderer/extensions/api/tabs_hooks_delegate.h"
#include "components/guest_view/buildflags/buildflags.h"
#include "extensions/buildflags/buildflags.h"
#include "extensions/renderer/bindings/api_bindings_system.h"
#include "extensions/renderer/lazy_background_page_native_handler.h"
#include "extensions/renderer/module_system.h"
#include "extensions/renderer/native_extension_bindings_system.h"
#include "extensions/renderer/resource_bundle_source_map.h"
#include "extensions/renderer/script_context.h"

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "build/chromeos_buildflags.h"
#include "chrome/renderer/extensions/api/app_hooks_delegate.h"
#include "chrome/renderer/extensions/api/identity_hooks_delegate.h"
#include "chrome/renderer/extensions/api/sync_file_system_custom_bindings.h"
#include "extensions/renderer/dispatcher.h"
#include "extensions/renderer/native_handler.h"
#include "printing/buildflags/buildflags.h"

#endif  // BUILDFLAG(ENABLE_EXTENSIONS)

namespace extensions {

void ChromeExtensionsRendererAPIProvider::RegisterNativeHandlers(
    ModuleSystem* module_system,
    NativeExtensionBindingsSystem* bindings_system,
    V8SchemaRegistry* v8_schema_registry,
    ScriptContext* context) const {
  // TODO(crbug.com/356905053): Move handlers supported on desktop android here.
  module_system->RegisterNativeHandler(
      "notifications_private",
      std::make_unique<NotificationsNativeHandler>(context));
  module_system->RegisterNativeHandler(
      "page_capture", std::make_unique<PageCaptureCustomBindings>(
                          context, bindings_system->GetIPCMessageSender()));

  // The following are native handlers that are defined in //extensions, but
  // are only used for APIs defined in Chrome.
  // TODO(devlin): We should clean this up. If an API is defined in Chrome,
  // there's no reason to have its native handlers residing and being compiled
  // in //extensions.
  module_system->RegisterNativeHandler(
      "lazy_background_page",
      std::make_unique<LazyBackgroundPageNativeHandler>(context));

#if BUILDFLAG(ENABLE_EXTENSIONS)
  module_system->RegisterNativeHandler(
      "sync_file_system",
      std::make_unique<SyncFileSystemCustomBindings>(context));
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)
}

void ChromeExtensionsRendererAPIProvider::AddBindingsSystemHooks(
    Dispatcher* dispatcher,
    NativeExtensionBindingsSystem* bindings_system) const {
  // TODO(crbug.com/356905053): Move bindings supported on desktop android here.
  APIBindingsSystem* bindings = bindings_system->api_system();
  bindings->RegisterHooksDelegate(
      "extension", std::make_unique<extensions::ExtensionHooksDelegate>(
                       bindings_system->messaging_service()));
  bindings->RegisterHooksDelegate(
      "tabs", std::make_unique<extensions::TabsHooksDelegate>(
                  bindings_system->messaging_service()));
#if BUILDFLAG(ENABLE_EXTENSIONS)
  bindings->RegisterHooksDelegate(
      "app", std::make_unique<extensions::AppHooksDelegate>(
                 dispatcher, bindings->request_handler(),
                 bindings_system->GetIPCMessageSender()));
  bindings->RegisterHooksDelegate(
      "identity", std::make_unique<extensions::IdentityHooksDelegate>());
#endif  // BUILDFLAG(ENABLE_EXTENSIONS)
}

void ChromeExtensionsRendererAPIProvider::PopulateSourceMap(
    ResourceBundleSourceMap* source_map) const {
  struct RegisterSourceData {
    std::string_view name;
    int resource_id;
  };

  static constexpr RegisterSourceData kSources[] = {
      // Custom bindings.
      {"action", IDR_ACTION_CUSTOM_BINDINGS_JS},
      {"browserAction", IDR_BROWSER_ACTION_CUSTOM_BINDINGS_JS},
      {"declarativeContent", IDR_DECLARATIVE_CONTENT_CUSTOM_BINDINGS_JS},
      {"desktopCapture", IDR_DESKTOP_CAPTURE_CUSTOM_BINDINGS_JS},
      {"developerPrivate", IDR_DEVELOPER_PRIVATE_CUSTOM_BINDINGS_JS},
      {"downloads", IDR_DOWNLOADS_CUSTOM_BINDINGS_JS},
      {"gcm", IDR_GCM_CUSTOM_BINDINGS_JS},
      {"identity", IDR_IDENTITY_CUSTOM_BINDINGS_JS},
      {"imageWriterPrivate", IDR_IMAGE_WRITER_PRIVATE_CUSTOM_BINDINGS_JS},
      {"input.ime", IDR_INPUT_IME_CUSTOM_BINDINGS_JS},
      {"mediaGalleries", IDR_MEDIA_GALLERIES_CUSTOM_BINDINGS_JS},
      {"notifications", IDR_NOTIFICATIONS_CUSTOM_BINDINGS_JS},
      {"omnibox", IDR_OMNIBOX_CUSTOM_BINDINGS_JS},
      {"pageAction", IDR_PAGE_ACTION_CUSTOM_BINDINGS_JS},
      {"pageCapture", IDR_PAGE_CAPTURE_CUSTOM_BINDINGS_JS},
      {"syncFileSystem", IDR_SYNC_FILE_SYSTEM_CUSTOM_BINDINGS_JS},
      {"tabCapture", IDR_TAB_CAPTURE_CUSTOM_BINDINGS_JS},
      {"tts", IDR_TTS_CUSTOM_BINDINGS_JS},
      {"ttsEngine", IDR_TTS_ENGINE_CUSTOM_BINDINGS_JS},


      {"webrtcDesktopCapturePrivate",
       IDR_WEBRTC_DESKTOP_CAPTURE_PRIVATE_CUSTOM_BINDINGS_JS},
      {"webrtcLoggingPrivate", IDR_WEBRTC_LOGGING_PRIVATE_CUSTOM_BINDINGS_JS},

      // Platform app sources that are not API-specific..
      {"chromeWebViewContextMenusApiMethods",
       IDR_CHROME_WEB_VIEW_CONTEXT_MENUS_API_METHODS_JS},
      {"chromeWebViewElement", IDR_CHROME_WEB_VIEW_ELEMENT_JS},
      {"chromeWebViewInternal",
       IDR_CHROME_WEB_VIEW_INTERNAL_CUSTOM_BINDINGS_JS},
      {"chromeWebView", IDR_CHROME_WEB_VIEW_JS},
  };

  for (const auto& source : kSources) {
    source_map->RegisterSource(source.name, source.resource_id);
  }
}

void ChromeExtensionsRendererAPIProvider::EnableCustomElementAllowlist() const {
}

void ChromeExtensionsRendererAPIProvider::RequireWebViewModules(
    ScriptContext* context) const {
#if BUILDFLAG(ENABLE_GUEST_VIEW)
  DCHECK(context->GetAvailability("webViewInternal").is_available());
  if (context->GetAvailability("chromeWebViewTag").is_available()) {
    // CHECK that the Chrome WebView and Controlled Frame features aren't both
    // enabled in the same context. This is here because Controlled Frame
    // is based on WebView and modifies base classes in order to not ship some
    // APIs. These modifications could harm a live WebView instance if we
    // allowed both in a single instance, but these features aren't designed
    // to be enabled in the same instance. This check confirms that is held.
    CHECK(!context->GetAvailability("controlledFrameInternal").is_available());

    context->module_system()->Require("chromeWebViewElement");
  }
#endif  // BUILDFLAG(ENABLE_GUEST_VIEW)
}

}  // namespace extensions
