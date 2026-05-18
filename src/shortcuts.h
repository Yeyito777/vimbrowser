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

typedef struct VimbrowserShortcut {
  VimbrowserShortcutAction action;
  const char* script;
} VimbrowserShortcut;

VimbrowserShortcut vimbrowser_shortcut_for_key(const char* url,
                                               unsigned int key,
                                               int is_raw_key_down,
                                               int is_char_event,
                                               int is_plain);

#ifdef __cplusplus
}
#endif
