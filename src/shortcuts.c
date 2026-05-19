#include "shortcuts.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static const char kYoutubeSeekBackScript[] =
    "(()=>{"
    "'use strict';"
    "const SEEK_SECONDS=5;"
    "function getActiveVideo(){"
    "const playerVideo=document.querySelector('#movie_player video,#movie_player .html5-main-video');"
    "if(playerVideo)return playerVideo;"
    "const videos=Array.from(document.querySelectorAll('video'));"
    "if(!videos.length)return null;"
    "return videos.filter(v=>v.readyState>0||v.currentSrc||v.src).sort((a,b)=>{"
    "const as=(a.clientWidth*a.clientHeight)+(a.paused?0:1000000);"
    "const bs=(b.clientWidth*b.clientHeight)+(b.paused?0:1000000);"
    "return bs-as;"
    "})[0]||null;"
    "}"
    "const video=getActiveVideo();"
    "if(!video)return;"
    "const max=Number.isFinite(video.duration)?video.duration:Infinity;"
    "video.currentTime=Math.max(0,Math.min(max,video.currentTime-SEEK_SECONDS));"
    "})();";

static const char kYoutubeTogglePlaybackScript[] =
    "(()=>{"
    "'use strict';"
    "function getActiveVideo(){"
    "const playerVideo=document.querySelector('#movie_player video,#movie_player .html5-main-video');"
    "if(playerVideo)return playerVideo;"
    "const videos=Array.from(document.querySelectorAll('video'));"
    "if(!videos.length)return null;"
    "return videos.filter(v=>v.readyState>0||v.currentSrc||v.src).sort((a,b)=>{"
    "const as=(a.clientWidth*a.clientHeight)+(a.paused?0:1000000);"
    "const bs=(b.clientWidth*b.clientHeight)+(b.paused?0:1000000);"
    "return bs-as;"
    "})[0]||null;"
    "}"
    "const player=document.querySelector('#movie_player');"
    "if(player&&typeof player.getPlayerState==='function'){"
    "const state=player.getPlayerState();"
    "if(state===1&&typeof player.pauseVideo==='function'){player.pauseVideo();return;}"
    "if(typeof player.playVideo==='function'){player.playVideo();return;}"
    "}"
    "const video=getActiveVideo();"
    "if(!video)return;"
    "if(video.paused){const promise=video.play&&video.play();if(promise&&promise.catch)promise.catch(()=>{});}"
    "else if(video.pause)video.pause();"
    "})();";

static const char kYoutubeSeekForwardScript[] =
    "(()=>{"
    "'use strict';"
    "const SEEK_SECONDS=5;"
    "function getActiveVideo(){"
    "const playerVideo=document.querySelector('#movie_player video,#movie_player .html5-main-video');"
    "if(playerVideo)return playerVideo;"
    "const videos=Array.from(document.querySelectorAll('video'));"
    "if(!videos.length)return null;"
    "return videos.filter(v=>v.readyState>0||v.currentSrc||v.src).sort((a,b)=>{"
    "const as=(a.clientWidth*a.clientHeight)+(a.paused?0:1000000);"
    "const bs=(b.clientWidth*b.clientHeight)+(b.paused?0:1000000);"
    "return bs-as;"
    "})[0]||null;"
    "}"
    "const video=getActiveVideo();"
    "if(!video)return;"
    "const max=Number.isFinite(video.duration)?video.duration:Infinity;"
    "video.currentTime=Math.max(0,Math.min(max,video.currentTime+SEEK_SECONDS));"
    "})();";

static const char kYoutubeVolumeDownScript[] =
    "(()=>{"
    "'use strict';"
    "const VOLUME_STEP=5;"
    "const player=document.querySelector('#movie_player');"
    "if(player&&typeof player.setVolume==='function'&&typeof player.getVolume==='function'){"
    "const vol=Math.max(0,Math.min(100,player.getVolume()-VOLUME_STEP));"
    "player.setVolume(vol);"
    "if(player.isMuted&&player.isMuted()&&vol>0&&player.unMute)player.unMute();"
    "return;"
    "}"
    "const video=document.querySelector('#movie_player video,#movie_player .html5-main-video')||document.querySelector('video');"
    "if(!video)return;"
    "video.volume=Math.max(0,Math.min(1,video.volume-(VOLUME_STEP/100)));"
    "if(video.muted&&video.volume>0)video.muted=false;"
    "})();";

static const char kYoutubeVolumeUpScript[] =
    "(()=>{"
    "'use strict';"
    "const VOLUME_STEP=5;"
    "const player=document.querySelector('#movie_player');"
    "if(player&&typeof player.setVolume==='function'&&typeof player.getVolume==='function'){"
    "const vol=Math.max(0,Math.min(100,player.getVolume()+VOLUME_STEP));"
    "player.setVolume(vol);"
    "if(player.isMuted&&player.isMuted()&&vol>0&&player.unMute)player.unMute();"
    "return;"
    "}"
    "const video=document.querySelector('#movie_player video,#movie_player .html5-main-video')||document.querySelector('video');"
    "if(!video)return;"
    "video.volume=Math.max(0,Math.min(1,video.volume+(VOLUME_STEP/100)));"
    "if(video.muted&&video.volume>0)video.muted=false;"
    "})();";

typedef struct PageShortcutBinding {
  unsigned int key;
  unsigned int modes;
  VimbrowserShortcutAction raw_action;
  VimbrowserShortcutAction char_action;
  const char* script;
} PageShortcutBinding;

typedef struct PageShortcutTable {
  int (*matches_url)(const char* url);
  const PageShortcutBinding* bindings;
  size_t binding_count;
} PageShortcutTable;

static int ascii_equal_ci(char a, char b) {
  return tolower((unsigned char)a) == tolower((unsigned char)b);
}

static int host_equals_ci(const char* host, size_t host_len, const char* value) {
  size_t value_len = strlen(value);
  if (host_len != value_len) {
    return 0;
  }
  for (size_t i = 0; i < host_len; ++i) {
    if (!ascii_equal_ci(host[i], value[i])) {
      return 0;
    }
  }
  return 1;
}

static int host_ends_with_ci(const char* host, size_t host_len, const char* suffix) {
  size_t suffix_len = strlen(suffix);
  if (host_len <= suffix_len) {
    return 0;
  }
  const size_t offset = host_len - suffix_len;
  if (host[offset - 1] != '.') {
    return 0;
  }
  for (size_t i = 0; i < suffix_len; ++i) {
    if (!ascii_equal_ci(host[offset + i], suffix[i])) {
      return 0;
    }
  }
  return 1;
}

static int url_host_bounds(const char* url, const char** host, size_t* host_len) {
  const char* p = url;
  const char* scheme = strstr(p, "://");
  if (scheme) {
    p = scheme + 3;
  }
  if (!*p) {
    return 0;
  }
  const char* at = strchr(p, '@');
  const char* slash = strpbrk(p, "/?#");
  if (at && (!slash || at < slash)) {
    p = at + 1;
  }
  const char* end = p;
  while (*end && *end != '/' && *end != '?' && *end != '#' && *end != ':') {
    ++end;
  }
  if (end == p) {
    return 0;
  }
  *host = p;
  *host_len = (size_t)(end - p);
  return 1;
}

static int matches_youtube(const char* url) {
  const char* host = NULL;
  size_t host_len = 0;
  if (!url_host_bounds(url, &host, &host_len)) {
    return 0;
  }
  return host_equals_ci(host, host_len, "youtube.com") ||
         host_ends_with_ci(host, host_len, "youtube.com");
}

static const PageShortcutBinding kYoutubeBindings[] = {
    {' ', VIMBROWSER_SHORTCUT_MODE_WEBSITE_NORMAL |
              VIMBROWSER_SHORTCUT_MODE_NORMAL,
     VIMBROWSER_SHORTCUT_EVALUATE_SCRIPT,
     VIMBROWSER_SHORTCUT_CONSUME, kYoutubeTogglePlaybackScript},
    {'h', VIMBROWSER_SHORTCUT_MODE_INSERT,
     VIMBROWSER_SHORTCUT_EVALUATE_SCRIPT,
     VIMBROWSER_SHORTCUT_CONSUME, kYoutubeSeekBackScript},
    {'j', VIMBROWSER_SHORTCUT_MODE_INSERT,
     VIMBROWSER_SHORTCUT_EVALUATE_SCRIPT,
     VIMBROWSER_SHORTCUT_CONSUME, kYoutubeVolumeDownScript},
    {'k', VIMBROWSER_SHORTCUT_MODE_INSERT,
     VIMBROWSER_SHORTCUT_EVALUATE_SCRIPT,
     VIMBROWSER_SHORTCUT_CONSUME, kYoutubeVolumeUpScript},
    {'l', VIMBROWSER_SHORTCUT_MODE_INSERT,
     VIMBROWSER_SHORTCUT_EVALUATE_SCRIPT,
     VIMBROWSER_SHORTCUT_CONSUME, kYoutubeSeekForwardScript},
};

static const PageShortcutTable kPageShortcuts[] = {
    {matches_youtube, kYoutubeBindings,
     sizeof(kYoutubeBindings) / sizeof(kYoutubeBindings[0])},
};

static VimbrowserShortcut shortcut_none(void) {
  VimbrowserShortcut shortcut;
  shortcut.action = VIMBROWSER_SHORTCUT_NONE;
  shortcut.script = NULL;
  return shortcut;
}

VimbrowserShortcut vimbrowser_shortcut_for_key(const char* url,
                                               unsigned int key,
                                               int is_raw_key_down,
                                               int is_char_event,
                                               int is_plain,
                                               unsigned int mode) {
  if (!url || !*url || !is_plain || !mode ||
      (!is_raw_key_down && !is_char_event)) {
    return shortcut_none();
  }

  const unsigned int normalized_key =
      (key >= 'A' && key <= 'Z') ? key + ('a' - 'A') : key;

  for (size_t table_index = 0;
       table_index < sizeof(kPageShortcuts) / sizeof(kPageShortcuts[0]);
       ++table_index) {
    const PageShortcutTable* table = &kPageShortcuts[table_index];
    if (!table->matches_url(url)) {
      continue;
    }
    for (size_t binding_index = 0; binding_index < table->binding_count;
         ++binding_index) {
      const PageShortcutBinding* binding = &table->bindings[binding_index];
      if (binding->key != normalized_key) {
        continue;
      }
      if (!(binding->modes & mode)) {
        continue;
      }
      VimbrowserShortcut shortcut;
      shortcut.action = is_raw_key_down ? binding->raw_action : binding->char_action;
      shortcut.script = shortcut.action == VIMBROWSER_SHORTCUT_EVALUATE_SCRIPT
                            ? binding->script
                            : NULL;
      return shortcut;
    }
  }

  return shortcut_none();
}
