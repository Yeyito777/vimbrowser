// macOS application bootstrap for the vimbrowser browser process.
//
// CEF on macOS requires the NSApplication instance to conform to
// CefAppProtocol so CEF can track re-entrant event dispatch.
#import <Cocoa/Cocoa.h>

#include <cstddef>
#include <string>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_application_mac.h"

// Implemented by main.cc. The callback copies all strings before returning and
// forwards them to the browser's canonical IPC endpoint off the AppKit thread.
extern "C" void VimbrowserOpenMacUrls(const char* const* urls, size_t count);

@interface VimBrowserApplicationDelegate : NSObject <NSApplicationDelegate>
@end

@implementation VimBrowserApplicationDelegate
- (void)application:(NSApplication*)application
            openURLs:(NSArray<NSURL*>*)urls {
  std::vector<std::string> url_strings;
  url_strings.reserve([urls count]);
  for (NSURL* url in urls) {
    NSString* absolute_string = [url absoluteString];
    if (!absolute_string) {
      continue;
    }
    const char* utf8 = [absolute_string UTF8String];
    if (utf8 && *utf8) {
      url_strings.emplace_back(utf8);
    }
  }

  std::vector<const char*> url_pointers;
  url_pointers.reserve(url_strings.size());
  for (const std::string& url : url_strings) {
    url_pointers.push_back(url.c_str());
  }
  if (!url_pointers.empty()) {
    VimbrowserOpenMacUrls(url_pointers.data(), url_pointers.size());
  }
}
@end

// NSApplication does not retain its delegate. Keep the application-lifetime
// ownership obtained from alloc/init so URL events continue to be delivered.
static VimBrowserApplicationDelegate* gApplicationDelegate = nil;

@interface VimBrowserApplication : NSApplication <CefAppProtocol> {
 @private
  BOOL handlingSendEvent_;
}
@end

@implementation VimBrowserApplication
- (BOOL)isHandlingSendEvent {
  return handlingSendEvent_;
}

- (void)setHandlingSendEvent:(BOOL)handlingSendEvent {
  handlingSendEvent_ = handlingSendEvent;
}

- (void)sendEvent:(NSEvent*)event {
  CefScopedSendingEvent sendingEventScoper;
  [super sendEvent:event];
}

// Cmd-Q / dock Quit: end CefRunMessageLoop() in main() so CefShutdown() runs
// on the way out instead of NSApplication terminating the process abruptly.
- (void)terminate:(id)sender {
  CefQuitMessageLoop();
}
@end

extern "C" void VimbrowserInitMacApplication() {
  VimBrowserApplication* application =
      [VimBrowserApplication sharedApplication];
  if (!gApplicationDelegate) {
    gApplicationDelegate = [[VimBrowserApplicationDelegate alloc] init];
  }
  [application setDelegate:gApplicationDelegate];
}
