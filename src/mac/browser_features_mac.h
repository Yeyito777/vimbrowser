#pragma once

#if !defined(__APPLE__)
#error "browser_features_mac.h is macOS-only"
#endif

#include <string>
#include <string_view>

namespace vimbrowser::mac {

inline constexpr char kFpsMonitorScript[] = R"JS(
(() => {
  if (window.__vimbrowserFpsMonitorStarted) return;
  window.__vimbrowserFpsMonitorStarted = true;
  let frames = 0;
  let started = performance.now();
  let previous = started;
  let intervals = [];
  let estimatedRefresh = 0;
  const tick = now => {
    frames += 1;
    const interval = now - previous;
    previous = now;
    if (interval > 0 && interval < 100) intervals.push(interval);
    const elapsed = now - started;
    if (elapsed >= 750) {
      const fps = frames * 1000 / elapsed;
      intervals.sort((a, b) => a - b);
      const median = intervals.length
        ? intervals[Math.floor(intervals.length / 2)]
        : 0;
      const observedRefresh = Math.max(fps, median > 0 ? 1000 / median : 0);
      if (observedRefresh > 0 && observedRefresh <= 360) {
        estimatedRefresh = Math.max(estimatedRefresh, observedRefresh);
      }
      console.debug('__vimbrowser_mac_fps__' + fps.toFixed(3) + ',' +
                    estimatedRefresh.toFixed(3));
      frames = 0;
      started = now;
      intervals = [];
    }
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
})();
)JS";

// Blink's Linux hint collector can query EventTarget's internal listener map.
// Stock macOS CEF does not expose that map, so record page listeners from the
// moment each V8 context is created and let the JS hint fallback query it.
inline constexpr char kHintListenerTrackerScript[] = R"JS(
(() => {
  if (typeof window.__vimbrowserHasListener === 'function') return;
  const listeners = new WeakMap();
  const closedShadowRoots = new WeakMap();
  const prototype = EventTarget.prototype;
  const nativeAdd = prototype.addEventListener;
  const nativeRemove = prototype.removeEventListener;
  const nativeAttachShadow = Element.prototype.attachShadow;
  const captureFor = options =>
    typeof options === 'boolean' ? options : Boolean(options && options.capture);
  const recordsFor = (target, type, create = false) => {
    let byType = listeners.get(target);
    if (!byType && create) {
      byType = new Map();
      listeners.set(target, byType);
    }
    if (!byType) return null;
    let records = byType.get(type);
    if (!records && create) {
      records = [];
      byType.set(type, records);
    }
    return records || null;
  };
  Object.defineProperty(prototype, 'addEventListener', {
    configurable: true,
    writable: true,
    value: function(type, listener, options) {
      const normalized = String(type).toLowerCase();
      const signal = options && typeof options === 'object' ? options.signal : null;
      if (listener && !(signal && signal.aborted)) {
        const capture = captureFor(options);
        const records = recordsFor(this, normalized, true);
        if (!records.some(record =>
              record.listener === listener && record.capture === capture)) {
          const record = {listener, capture};
          records.push(record);
          if (signal && typeof signal.addEventListener === 'function') {
            Reflect.apply(nativeAdd, signal, ['abort', () => {
              const index = records.indexOf(record);
              if (index >= 0) records.splice(index, 1);
            }, {once: true}]);
          }
        }
      }
      return Reflect.apply(nativeAdd, this, arguments);
    }
  });
  Object.defineProperty(prototype, 'removeEventListener', {
    configurable: true,
    writable: true,
    value: function(type, listener, options) {
      const normalized = String(type).toLowerCase();
      const capture = captureFor(options);
      const records = recordsFor(this, normalized);
      if (records) {
        const index = records.findIndex(record =>
          record.listener === listener && record.capture === capture);
        if (index >= 0) records.splice(index, 1);
      }
      return Reflect.apply(nativeRemove, this, arguments);
    }
  });
  Object.defineProperty(window, '__vimbrowserHasListener', {
    configurable: false,
    enumerable: false,
    writable: false,
    value: (target, types) => Array.isArray(types) && types.some(type => {
      const records = recordsFor(target, String(type).toLowerCase());
      return Boolean(records && records.length);
    })
  });
  Object.defineProperty(Element.prototype, 'attachShadow', {
    configurable: true,
    writable: true,
    value: function(init) {
      const root = Reflect.apply(nativeAttachShadow, this, arguments);
      closedShadowRoots.set(this, root);
      return root;
    }
  });
  Object.defineProperty(window, '__vimbrowserShadowRoot', {
    configurable: false,
    enumerable: false,
    writable: false,
    value: host => host && (host.shadowRoot || closedShadowRoots.get(host) || null)
  });
})()
)JS";

inline std::string BuildHintScript(std::string_view mode) {
  std::string script = "(()=>{const mode='";
  script.append(mode);
  script += R"JS(';
  const report = (event, payload = '') => {
    if (typeof window.__vimbrowserReport === 'function') {
      window.__vimbrowserReport(event, String(payload));
    }
  };
  if (typeof window.__vimbrowserHintsCleanup === 'function') {
    window.__vimbrowserHintsCleanup(false);
  }
  const alphabet = 'asdfghjklqwertyuiopzxcvbnm';
  const clickSelector =
    'a,area,textarea,select,input:not([type="hidden"]),button,frame,iframe,' +
    'img,link,summary,[contenteditable]:not([contenteditable="false"]),' +
    '[onclick],[onmousedown],[role="link"],[role="option"],[role="button"],' +
    '[role="tab"],[role="checkbox"],[role="switch"],[role="menuitem"],' +
    '[role="menuitemcheckbox"],[role="menuitemradio"],[role="treeitem"],' +
    '[aria-haspopup],[ng-click],[ngClick],[data-ng-click],[x-ng-click],' +
    '[tabindex]:not([tabindex="-1"])';
  const contextSelector =
    'a,area,link,img,audio,video,frame,iframe,textarea,' +
    'input:not([type="hidden"]),' +
    '[contenteditable]:not([contenteditable="false"]),' +
    '[oncontextmenu],[onauxclick],[onmousedown],[onmouseup],[onpointerdown],' +
    '[onpointerup],[data-context-menu],[data-contextmenu]';
  const hoverSelector =
    '[title],[data-tooltip],[data-tip],[aria-describedby],[onmouseover],' +
    '[onmouseenter],[onmousemove],[role="article"],[aria-roledescription],' +
    'abbr,acronym,iframe,frame,video';
  const clickEvents = [
    'onclick', 'onauxclick', 'ondblclick', 'onmousedown', 'onmouseup',
    'onpointerdown', 'onpointerup', 'ontouchstart', 'ontouchend'
  ];
  const contextEvents = [
    'oncontextmenu', 'onauxclick', 'onmousedown', 'onmouseup',
    'onpointerdown', 'onpointerup'
  ];
  const hoverEvents = [
    'onmouseenter', 'onmouseover', 'onmousemove',
    'onpointerenter', 'onpointerover', 'onpointermove'
  ];
  const labelFor = (index, width) => {
    let label = '';
    for (let digit = 0; digit < width; ++digit) {
      label = alphabet[index % alphabet.length] + label;
      index = Math.floor(index / alphabet.length);
    }
    return label;
  };
  const composedParent = element => {
    if (element.parentElement) return element.parentElement;
    const root = element.getRootNode && element.getRootNode();
    return root instanceof ShadowRoot ? root.host : null;
  };
  const shadowRootFor = element =>
    typeof window.__vimbrowserShadowRoot === 'function'
      ? window.__vimbrowserShadowRoot(element)
      : element.shadowRoot;
  const collectDeepElements = root => {
    const found = [];
    const visitChildren = node => {
      for (const child of Array.from(node.children || [])) {
        found.push(child);
        const shadowRoot = shadowRootFor(child);
        if (shadowRoot) visitChildren(shadowRoot);
        visitChildren(child);
      }
    };
    if (root.documentElement) {
      found.push(root.documentElement);
      const documentShadowRoot = shadowRootFor(root.documentElement);
      if (documentShadowRoot) {
        visitChildren(documentShadowRoot);
      }
      visitChildren(root.documentElement);
    } else {
      visitChildren(root);
    }
    return found;
  };
  const allElements = collectDeepElements(document);
  const rectIsVisible = rect =>
    rect && rect.width >= 4 && rect.height >= 4 && rect.bottom > 0 &&
    rect.right > 0 && rect.top < innerHeight && rect.left < innerWidth;
  const allowsZeroOpacity = element =>
    element.classList.contains('ace_text-input') ||
    element.classList.contains('custom-control-input');
  const hasVisibleStyle = element => {
    if (!(element instanceof Element)) return false;
    const elementStyle = getComputedStyle(element);
    if (elementStyle.visibility !== 'visible' ||
        elementStyle.display === 'none') return false;
    for (let ancestor = element; ancestor; ancestor = composedParent(ancestor)) {
      const style = getComputedStyle(ancestor);
      if (Number(style.opacity) === 0 &&
          (ancestor !== element || !allowsZeroOpacity(element))) return false;
    }
    return true;
  };
  const descendantsOf = root => {
    const descendants = [];
    const visit = node => {
      for (const child of Array.from(node.children || [])) {
        descendants.push(child);
        const shadowRoot = shadowRootFor(child);
        if (shadowRoot) visit(shadowRoot);
        visit(child);
      }
    };
    const rootShadow = shadowRootFor(root);
    if (rootShadow) visit(rootShadow);
    visit(root);
    return descendants;
  };
  const visibleRect = element => {
    const rect = element.getBoundingClientRect();
    return rectIsVisible(rect) ? rect : null;
  };
  const candidateRect = element => {
    if (element.localName === 'a') {
      for (const heading of descendantsOf(element)) {
        if (/^h[1-6]$/.test(heading.localName)) {
          const headingRect = visibleRect(heading);
          if (headingRect) return headingRect;
        }
      }
    }
    const ownRect = visibleRect(element);
    if (ownRect) return ownRect;
    if (element.localName !== 'a') return null;
    let best = null;
    let bestArea = 0;
    for (const descendant of descendantsOf(element)) {
      const rect = visibleRect(descendant);
      if (!rect) continue;
      const area = rect.width * rect.height;
      if (area > bestArea) {
        best = rect;
        bestArea = area;
      }
    }
    return best;
  };
  const hasDomHandler = (element, names) =>
    names.some(name =>
      typeof element[name] === 'function' || element.hasAttribute(name)) ||
    (typeof window.__vimbrowserHasListener === 'function' &&
     window.__vimbrowserHasListener(element,
       names.map(name => name.startsWith('on') ? name.slice(2) : name)));
  const isDocumentScroller = element =>
    element === document.scrollingElement || element === document.documentElement ||
    element === document.body;
  const hasOwnPointerCursor = element => {
    if (getComputedStyle(element).cursor !== 'pointer') return false;
    const parent = composedParent(element);
    return !parent || getComputedStyle(parent).cursor !== 'pointer';
  };
  const isScrollable = element => {
    const y = element.scrollHeight > element.clientHeight;
    const x = element.scrollWidth > element.clientWidth;
    if (!x && !y) return false;
    const style = getComputedStyle(element);
    if (isDocumentScroller(element)) {
      return (y && style.overflowY !== 'hidden' && style.overflowY !== 'clip') ||
             (x && style.overflowX !== 'hidden' && style.overflowX !== 'clip');
    }
    const scrollsOverflow = value =>
      value === 'auto' || value === 'scroll' || value === 'overlay';
    return (y && scrollsOverflow(style.overflowY)) ||
           (x && scrollsOverflow(style.overflowX));
  };
  const matchesMode = element => {
    if (mode === 'scroll') return isScrollable(element);
    if (mode === 'hover') {
      return element.matches(hoverSelector) || hasDomHandler(element, hoverEvents);
    }
    if (mode === 'context') {
      return element.matches(contextSelector) ||
             hasDomHandler(element, contextEvents);
    }
    return element.matches(clickSelector) || hasDomHandler(element, clickEvents) ||
           (!isDocumentScroller(element) && hasOwnPointerCursor(element));
  };
  const toCandidate = element => {
    if (!matchesMode(element) || !hasVisibleStyle(element)) return null;
    const pageScroller = mode === 'scroll' && isDocumentScroller(element);
    const rect = pageScroller
      ? {left: 0, top: 0, right: innerWidth, bottom: innerHeight,
         width: innerWidth, height: innerHeight}
      : candidateRect(element);
    return rect ? {element, rect, marker: null, label: ''} : null;
  };
  let candidates = [];
  if (mode === 'scroll') {
    const page = document.scrollingElement || document.documentElement;
    if (page) {
      const candidate = toCandidate(page);
      if (candidate) candidates.push(candidate);
    }
  }
  for (const element of allElements) {
    if (mode === 'scroll' && isDocumentScroller(element)) continue;
    const candidate = toCandidate(element);
    if (candidate) candidates.push(candidate);
  }
  if (mode === 'click' || mode === 'new-tab') {
    const isComposedDescendant = (element, ancestor) => {
      for (let current = composedParent(element); current;
           current = composedParent(current)) {
        if (current === ancestor) return true;
      }
      return false;
    };
    candidates = candidates.filter(candidate => {
      const element = candidate.element;
      const listenerOnly = hasDomHandler(element, clickEvents) &&
        !element.matches(clickSelector) && !hasOwnPointerCursor(element);
      if (!listenerOnly) return true;
      return !candidates.some(other => other !== candidate &&
        hasOwnPointerCursor(other.element) &&
        isComposedDescendant(other.element, element));
    });
  }
  let labelWidth = 1;
  while (alphabet.length ** labelWidth < candidates.length) labelWidth += 1;
  candidates.forEach((candidate, index) => {
    candidate.label = labelFor(index, labelWidth);
  });
  if (mode === 'scroll' && labelWidth === 1 && candidates.length) {
    const fIndex = candidates.findIndex((candidate, index) =>
      index > 0 && candidate.label === 'f');
    if (fIndex >= 0) {
      const original = candidates[0].label;
      candidates[0].label = 'f';
      candidates[fIndex].label = original;
    } else {
      candidates[0].label = 'f';
    }
  }
  if (!candidates.length) {
    report('stopped');
    return;
  }
  const overlay = document.createElement('div');
  overlay.id = '__vimbrowser-hints';
  overlay.style.cssText =
    'all:initial;display:block;position:fixed;inset:0;pointer-events:none;' +
    'z-index:2147483647;contain:strict;overflow:visible;';
  const renderMarker = candidate => {
    candidate.marker.textContent = candidate.label;
  };
  for (const candidate of candidates) {
    const rect = candidate.rect;
    const marker = document.createElement('span');
    marker.style.cssText =
      'all:initial;box-sizing:border-box;display:block;' +
      `position:absolute;left:${Math.max(0, rect.left)}px;` +
      `top:${Math.max(0, rect.top)}px;` +
      `width:${Math.max(12, candidate.label.length * 8)}px;height:13px;` +
      'padding:0;border:0;border-radius:0;background:#1d9bf0;color:#00050f;' +
      'box-shadow:none;font-family:"JetBrains Mono",monospace,' +
      '"DejaVu Sans Mono","Liberation Mono","Noto Sans Mono";' +
      'font-size:13px;font-style:normal;font-weight:700;line-height:13px;' +
      'letter-spacing:0;text-align:center;text-decoration:none;' +
      'text-transform:none;white-space:pre;';
    candidate.marker = marker;
    renderMarker(candidate);
    overlay.appendChild(marker);
  }
  document.documentElement.appendChild(overlay);
  let prefix = '';
  const cleanup = (notify = true) => {
    document.removeEventListener('keydown', onKey, true);
    overlay.remove();
    delete window.__vimbrowserHintsCleanup;
    if (notify) report('stopped');
  };
  const closestAnchor = element => {
    for (let current = element; current; current = composedParent(current)) {
      if (current.matches && current.matches('a[href],area[href]')) return current;
    }
    return null;
  };
  const activate = candidate => {
    const element = candidate.element;
    cleanup(false);
    if (mode === 'scroll') {
      window.__vimbrowserScrollTarget = element;
    } else if (mode === 'new-tab') {
      const anchor = closestAnchor(element);
      if (anchor && anchor.href) report('open-tab', anchor.href);
      else if (typeof element.click === 'function') element.click();
    } else if (mode === 'context') {
      const rect = candidate.rect;
      report('context-at', `${rect.left + rect.width / 2},` +
                           `${rect.top + rect.height / 2}`);
    } else if (mode === 'hover') {
      const rect = candidate.rect;
      report('hover-at', `${rect.left + rect.width / 2},` +
                         `${rect.top + rect.height / 2}`);
    } else if (typeof element.click === 'function') {
      element.click();
    }
    if (element.matches &&
        (element.matches('input,textarea,select') || element.isContentEditable)) {
      element.focus();
      report('focused-editable');
    }
    report('stopped');
  };
  function onKey(event) {
    const eventKey = String(event.key || '');
    const normalizedKey = eventKey.toLowerCase();
    if (normalizedKey === 'escape') {
      event.preventDefault();
      event.stopImmediatePropagation();
      cleanup();
      return;
    }
    event.preventDefault();
    event.stopImmediatePropagation();
    if (normalizedKey === 'backspace') {
      if (prefix) {
        prefix = prefix.slice(0, -1);
        for (const candidate of candidates) {
          candidate.marker.style.display =
            candidate.label.startsWith(prefix) ? 'block' : 'none';
          renderMarker(candidate);
        }
      }
      return;
    }
    const key = normalizedKey;
    if (key.length !== 1 || !alphabet.includes(key)) return;
    prefix += key;
    const matches = candidates.filter(candidate =>
      candidate.label.startsWith(prefix));
    for (const candidate of candidates) {
      candidate.marker.style.display = matches.includes(candidate) ? 'block' : 'none';
      renderMarker(candidate);
    }
    const exact = matches.find(candidate => candidate.label === prefix);
    if (exact) activate(exact);
    else if (!matches.length) cleanup();
  }
  window.__vimbrowserHintsCleanup = cleanup;
  document.addEventListener('keydown', onKey, true);
})()
)JS";
  return script;
}

}  // namespace vimbrowser::mac
