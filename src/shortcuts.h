#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VimbrowserShortcutAction {
  VIMBROWSER_SHORTCUT_NONE = 0,
  VIMBROWSER_SHORTCUT_FORWARD_TO_PAGE = 1,
  VIMBROWSER_SHORTCUT_CONSUME = 2,
  VIMBROWSER_SHORTCUT_EVALUATE_SCRIPT = 3,
} VimbrowserShortcutAction;

typedef enum VimbrowserShortcutMode {
  VIMBROWSER_SHORTCUT_MODE_WEBSITE_NORMAL = 1u << 0,
  VIMBROWSER_SHORTCUT_MODE_NORMAL = 1u << 1,
  VIMBROWSER_SHORTCUT_MODE_INSERT = 1u << 2,
} VimbrowserShortcutMode;

typedef struct VimbrowserShortcut {
  VimbrowserShortcutAction action;
  const char* script;
} VimbrowserShortcut;

VimbrowserShortcut vimbrowser_shortcut_for_key(const char* url,
                                               unsigned int key,
                                               int is_raw_key_down,
                                               int is_char_event,
                                               int is_plain,
                                               unsigned int mode);

#ifdef __cplusplus
}
#endif
