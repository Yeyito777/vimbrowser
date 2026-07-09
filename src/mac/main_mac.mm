// macOS application bootstrap for the vimbrowser browser process.
//
// CEF on macOS requires the NSApplication instance to conform to
// CefAppProtocol so CEF can track re-entrant event dispatch.
#import <Cocoa/Cocoa.h>

#include "include/cef_app.h"
#include "include/cef_application_mac.h"

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
  [VimBrowserApplication sharedApplication];
}
