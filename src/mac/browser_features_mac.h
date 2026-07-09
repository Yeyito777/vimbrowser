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
  const labelFor = (index, width) => {
    let label = '';
    for (let digit = 0; digit < width; ++digit) {
      label = alphabet[index % alphabet.length] + label;
      index = Math.floor(index / alphabet.length);
    }
    return label;
  };
  const visible = element => {
    if (!(element instanceof Element) || element.disabled) return false;
    const style = getComputedStyle(element);
    if (style.display === 'none' || style.visibility === 'hidden' ||
        Number(style.opacity) === 0) return false;
    const rect = element.getBoundingClientRect();
    return rect.width >= 3 && rect.height >= 3 && rect.bottom > 0 &&
           rect.right > 0 && rect.top < innerHeight && rect.left < innerWidth;
  };
  let elements;
  if (mode === 'scroll') {
    elements = Array.from(document.querySelectorAll('*')).filter(element => {
      if (!visible(element)) return false;
      const style = getComputedStyle(element);
      const y = element.scrollHeight > element.clientHeight + 4 &&
                style.overflowY !== 'hidden';
      const x = element.scrollWidth > element.clientWidth + 4 &&
                style.overflowX !== 'hidden';
      return x || y;
    });
    const page = document.scrollingElement || document.documentElement;
    if (page && !elements.includes(page) &&
        page.scrollHeight > page.clientHeight + 4) elements.unshift(page);
  } else {
    elements = Array.from(document.querySelectorAll(
      'a[href],button,input:not([type="hidden"]),select,textarea,summary,' +
      '[role="button"],[role="link"],[onclick],[tabindex]')).filter(visible);
  }
  let labelWidth = 1;
  while (alphabet.length ** labelWidth < elements.length) labelWidth += 1;
  const candidates = elements.map((element, index) => ({
    element,
    label: labelFor(index, labelWidth),
    marker: null,
  }));
  if (!candidates.length) {
    report('stopped');
    return;
  }
  const overlay = document.createElement('div');
  overlay.id = '__vimbrowser-hints';
  overlay.style.cssText =
    'position:fixed;inset:0;pointer-events:none;z-index:2147483647;' +
    'contain:strict;font:700 12px ui-monospace,SFMono-Regular,monospace;';
  for (const candidate of candidates) {
    const rect = candidate.element.getBoundingClientRect();
    const marker = document.createElement('span');
    marker.textContent = candidate.label;
    marker.style.cssText =
      `position:absolute;left:${Math.max(0, rect.left)}px;` +
      `top:${Math.max(0, rect.top)}px;padding:1px 4px;border:1px solid #111;` +
      'border-radius:2px;background:#39ff77;color:#07110a;' +
      'box-shadow:0 1px 3px #000b;line-height:16px;';
    candidate.marker = marker;
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
  const activate = candidate => {
    const element = candidate.element;
    cleanup(false);
    if (mode === 'scroll') {
      window.__vimbrowserScrollTarget = element;
    } else if (mode === 'new-tab') {
      const anchor = element.closest && element.closest('a[href]');
      if (anchor && anchor.href) report('open-tab', anchor.href);
      else if (typeof element.click === 'function') element.click();
    } else if (mode === 'context') {
      const rect = element.getBoundingClientRect();
      report('context-at', `${rect.left + rect.width / 2},` +
                           `${rect.top + rect.height / 2}`);
    } else if (mode === 'hover') {
      const rect = element.getBoundingClientRect();
      report('hover-at', `${rect.left + rect.width / 2},` +
                         `${rect.top + rect.height / 2}`);
    } else if (typeof element.click === 'function') {
      element.click();
    }
    if (element.matches &&
        element.matches('input,textarea,select,[contenteditable="true"]')) {
      element.focus();
      report('focused-editable');
    }
    report('stopped');
  };
  function onKey(event) {
    if (event.key === 'Escape') {
      event.preventDefault();
      event.stopImmediatePropagation();
      cleanup();
      return;
    }
    const key = event.key.toLowerCase();
    if (key.length !== 1 || !alphabet.includes(key)) return;
    event.preventDefault();
    event.stopImmediatePropagation();
    prefix += key;
    const matches = candidates.filter(candidate =>
      candidate.label.startsWith(prefix));
    for (const candidate of candidates) {
      candidate.marker.style.display = matches.includes(candidate) ? '' : 'none';
      candidate.marker.textContent = candidate.label.slice(prefix.length) || candidate.label;
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
