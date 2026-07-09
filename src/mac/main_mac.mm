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

- (void)vimbrowserDidBecomeActive:(NSNotification*)notification {
  (void)notification;
  [self setPresentationOptions:NSApplicationPresentationDefault];
  [NSMenu setMenuBarVisible:YES];
}

// Cmd-Q / dock Quit: end CefRunMessageLoop() in main() so CefShutdown() runs
// on the way out instead of NSApplication terminating the process abruptly.
- (void)terminate:(id)sender {
  CefQuitMessageLoop();
}
@end

namespace {

NSMenuItem* AddMenuItem(NSMenu* menu,
                        NSString* title,
                        SEL action,
                        NSString* key_equivalent) {
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                               action:action
                                        keyEquivalent:key_equivalent];
  [menu addItem:item];
  return item;
}

void InstallMainMenu(NSApplication* application) {
  if ([application mainMenu] && [[application mainMenu] numberOfItems] > 0) {
    return;
  }

  NSMenu* main_menu = [[NSMenu alloc] initWithTitle:@""];

  NSMenuItem* application_menu_item =
      [[NSMenuItem alloc] initWithTitle:@"vimbrowser"
                                 action:nil
                          keyEquivalent:@""];
  NSMenu* application_menu =
      [[NSMenu alloc] initWithTitle:@"vimbrowser"];
  AddMenuItem(application_menu, @"About vimbrowser",
              @selector(orderFrontStandardAboutPanel:), @"");
  [application_menu addItem:[NSMenuItem separatorItem]];

  NSMenuItem* services_item =
      AddMenuItem(application_menu, @"Services", nil, @"");
  NSMenu* services_menu = [[NSMenu alloc] initWithTitle:@"Services"];
  [services_item setSubmenu:services_menu];
  [application setServicesMenu:services_menu];
  [application_menu addItem:[NSMenuItem separatorItem]];

  AddMenuItem(application_menu, @"Hide vimbrowser", @selector(hide:), @"h");
  NSMenuItem* hide_others_item =
      AddMenuItem(application_menu, @"Hide Others",
                  @selector(hideOtherApplications:), @"h");
  [hide_others_item setKeyEquivalentModifierMask:
      NSEventModifierFlagOption | NSEventModifierFlagCommand];
  AddMenuItem(application_menu, @"Show All",
              @selector(unhideAllApplications:), @"");
  [application_menu addItem:[NSMenuItem separatorItem]];
  AddMenuItem(application_menu, @"Quit vimbrowser", @selector(terminate:), @"q");
  [application_menu_item setSubmenu:application_menu];
  [main_menu addItem:application_menu_item];

  NSMenuItem* edit_menu_item =
      [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
  NSMenu* edit_menu = [[NSMenu alloc] initWithTitle:@"Edit"];
  AddMenuItem(edit_menu, @"Undo", @selector(undo:), @"z");
  NSMenuItem* redo_item =
      AddMenuItem(edit_menu, @"Redo", @selector(redo:), @"z");
  [redo_item setKeyEquivalentModifierMask:
      NSEventModifierFlagShift | NSEventModifierFlagCommand];
  [edit_menu addItem:[NSMenuItem separatorItem]];
  AddMenuItem(edit_menu, @"Cut", @selector(cut:), @"x");
  AddMenuItem(edit_menu, @"Copy", @selector(copy:), @"c");
  AddMenuItem(edit_menu, @"Paste", @selector(paste:), @"v");
  AddMenuItem(edit_menu, @"Select All", @selector(selectAll:), @"a");
  [edit_menu_item setSubmenu:edit_menu];
  [main_menu addItem:edit_menu_item];

  NSMenuItem* window_menu_item =
      [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
  NSMenu* window_menu = [[NSMenu alloc] initWithTitle:@"Window"];
  AddMenuItem(window_menu, @"Minimize", @selector(performMiniaturize:), @"m");
  AddMenuItem(window_menu, @"Zoom", @selector(performZoom:), @"");
  [window_menu addItem:[NSMenuItem separatorItem]];
  AddMenuItem(window_menu, @"Bring All to Front",
              @selector(arrangeInFront:), @"");
  [window_menu_item setSubmenu:window_menu];
  [main_menu addItem:window_menu_item];
  [application setWindowsMenu:window_menu];

  [application setMainMenu:main_menu];
}

}  // namespace

extern "C" void VimbrowserInitMacApplication() {
  VimBrowserApplication* application =
      [VimBrowserApplication sharedApplication];
  [application setActivationPolicy:NSApplicationActivationPolicyRegular];
  [application setPresentationOptions:NSApplicationPresentationDefault];
  InstallMainMenu(application);
  [[NSNotificationCenter defaultCenter]
      addObserver:application
         selector:@selector(vimbrowserDidBecomeActive:)
             name:NSApplicationDidBecomeActiveNotification
           object:application];
  [NSMenu setMenuBarVisible:YES];
}
