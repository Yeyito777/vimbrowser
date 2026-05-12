#pragma once

namespace vimbrowser {

inline const char* ElementShaderScript() {
  return R"VIMSHADER(
(function(enabled){
  const STYLE_ID = '__vimbrowser_element_shader_style__';
  const STATE_KEY = '__vimbrowserElementShaderState';
  const ORIGINAL_STYLE = '__vimbrowserElementShaderOriginalStyle';
  const TOUCHED = '__vimbrowserElementShaderTouched';

  function parseColor(value) {
    if (!value || value === 'transparent') return null;
    const m = String(value).match(/rgba?\(([^)]+)\)/i);
    if (!m) return null;
    const parts = m[1].split(',').map(s => s.trim());
    if (parts.length < 3) return null;
    return {
      r: Math.max(0, Math.min(255, parseFloat(parts[0]) || 0)),
      g: Math.max(0, Math.min(255, parseFloat(parts[1]) || 0)),
      b: Math.max(0, Math.min(255, parseFloat(parts[2]) || 0)),
      a: parts.length >= 4 ? Math.max(0, Math.min(1, parseFloat(parts[3]) || 0)) : 1
    };
  }

  function cssColor(c) {
    return 'rgba(' + Math.round(c.r) + ',' + Math.round(c.g) + ',' + Math.round(c.b) + ',' + c.a + ')';
  }

  function rgbToHsl(r, g, b) {
    r /= 255; g /= 255; b /= 255;
    const max = Math.max(r, g, b), min = Math.min(r, g, b);
    let h = 0, s = 0;
    const l = (max + min) / 2;
    if (max !== min) {
      const d = max - min;
      s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
      switch (max) {
        case r: h = (g - b) / d + (g < b ? 6 : 0); break;
        case g: h = (b - r) / d + 2; break;
        case b: h = (r - g) / d + 4; break;
      }
      h /= 6;
    }
    return {h, s, l};
  }

  function hue2rgb(p, q, t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1/6) return p + (q - p) * 6 * t;
    if (t < 1/2) return q;
    if (t < 2/3) return p + (q - p) * (2/3 - t) * 6;
    return p;
  }

  function hslToRgb(h, s, l, a) {
    let r, g, b;
    if (s === 0) {
      r = g = b = l;
    } else {
      const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
      const p = 2 * l - q;
      r = hue2rgb(p, q, h + 1/3);
      g = hue2rgb(p, q, h);
      b = hue2rgb(p, q, h - 1/3);
    }
    return {r: r * 255, g: g * 255, b: b * 255, a};
  }

  function chroma(c) { return Math.max(c.r, c.g, c.b) - Math.min(c.r, c.g, c.b); }
  function brightness(c) { return (c.r + c.g + c.b) / (3 * 255); }
  function setImportant(style, property, value) { style.setProperty(property, value, 'important'); }

  function signalReady() {
    try {
      if (typeof window.__vimbrowserShaderReady === 'function') {
        window.__vimbrowserShaderReady();
      }
    } catch (_) {}
  }

  function installGlobalStyle() {
    let style = document.getElementById(STYLE_ID);
    if (!style) {
      style = document.createElement('style');
      style.id = STYLE_ID;
      style.textContent = `
        ::selection { background: #4f5258 !important; color: #ffffff !important; }
        ::-webkit-scrollbar { background: #00050f !important; border-radius: 0 !important; }
        ::-webkit-scrollbar-track, ::-webkit-scrollbar-track-piece, ::-webkit-scrollbar-corner,
        ::-webkit-scrollbar-button { background: #00050f !important; border-radius: 0 !important; }
        ::-webkit-scrollbar-thumb { background: #00050f !important; border: 1px solid #1d9bf0 !important; border-radius: 0 !important; }
        input, textarea, select, button { caret-color: #ffffff !important; }
        * { scrollbar-color: #1d9bf0 #00050f !important; }
      `;
      (document.head || document.documentElement).appendChild(style);
    }
  }

  function removeGlobalStyle() {
    const style = document.getElementById(STYLE_ID);
    if (style) style.remove();
  }

  function shouldSkip(el) {
    if (!el || el.nodeType !== 1) return true;
    const tag = el.localName;
    if (tag === 'script' || tag === 'style' || tag === 'template' || tag === 'meta' || tag === 'link') return true;
    if (el.closest && el.closest('[data-no-shader]')) return true;
    if (tag === 'svg' || (el.namespaceURI && el.namespaceURI.indexOf('svg') !== -1)) return true;
    return false;
  }

  function remember(el, state) {
    if (!(ORIGINAL_STYLE in el)) el[ORIGINAL_STYLE] = el.getAttribute('style');
    state.touched.add(el);
  }

  function applyElement(el, state) {
    if (shouldSkip(el)) return;
    remember(el, state);

    const cs = getComputedStyle(el);
    const st = el.style;
    const rect = el.getBoundingClientRect ? el.getBoundingClientRect() : {width:0, height:0};
    const tag = el.localName;
    const area = Math.max(0, rect.width || 0) * Math.max(0, rect.height || 0);
    const maxChromaticBgArea = 200000.0;
    let forceDark = tag === 'html' || tag === 'body' || area > maxChromaticBgArea;

    const fg = parseColor(cs.color);
    let fgOut = {r:255, g:255, b:255, a:1};
    if (fg && fg.a <= 0.001) {
      fgOut.a = 0;
    } else if (fg && chroma(fg) > 25) {
      const hsl = rgbToHsl(fg.r, fg.g, fg.b);
      fgOut = hslToRgb(hsl.h, Math.max(hsl.s, 0.70), Math.max(hsl.l, 0.70), fg.a);
    }
    const fgCss = cssColor(fgOut);
    setImportant(st, 'color', fgCss);
    setImportant(st, '-webkit-text-fill-color', fgCss);
    setImportant(st, 'caret-color', fgCss);
    setImportant(st, 'text-decoration-color', fgCss);
    setImportant(st, 'text-emphasis-color', fgCss);
    setImportant(st, '-webkit-text-stroke-color', fgCss);

    const borderColor = '#1d9bf0';
    if (parseFloat(cs.borderTopWidth) > 0) setImportant(st, 'border-top-color', borderColor);
    if (parseFloat(cs.borderRightWidth) > 0) setImportant(st, 'border-right-color', borderColor);
    if (parseFloat(cs.borderBottomWidth) > 0) setImportant(st, 'border-bottom-color', borderColor);
    if (parseFloat(cs.borderLeftWidth) > 0) setImportant(st, 'border-left-color', borderColor);
    setImportant(st, 'outline-color', borderColor);
    setImportant(st, 'border-radius', '0');

    if (cs.backgroundImage && cs.backgroundImage !== 'none' && /gradient/i.test(cs.backgroundImage)) {
      setImportant(st, 'background-image', 'linear-gradient(rgba(0,5,15,1), rgba(9,13,53,1))');
    }

    if (cs.boxShadow && cs.boxShadow !== 'none') {
      // Match the C++ shader's shadow hue (#090d35) while preserving geometry approximately.
      setImportant(st, 'box-shadow', cs.boxShadow.replace(/rgba?\([^)]*\)|#[0-9a-fA-F]{3,8}/g, function(token) {
        if (/rgba?\(/i.test(token)) {
          const c = parseColor(token);
          return c ? cssColor({r:9, g:13, b:53, a:c.a}) : token;
        }
        return 'rgba(9,13,53,1)';
      }));
    }

    const bg = parseColor(cs.backgroundColor);
    if (bg && bg.a > 0.001) {
      let bgOut;
      if (!forceDark && chroma(bg) > 25) {
        const hsl = rgbToHsl(bg.r, bg.g, bg.b);
        bgOut = hslToRgb(hsl.h, Math.max(hsl.s, 0.50), Math.min(hsl.l, 0.15), bg.a);
      } else {
        bgOut = {r:0, g:5, b:15, a:bg.a};
        if (!forceDark) {
          const b = brightness(bg);
          if (b > 0.5 && b < 0.95) {
            const parentBg = el.parentElement ? parseColor(getComputedStyle(el.parentElement).backgroundColor) : null;
            if (!parentBg || parentBg.a <= 0.001 || (parentBg.r < 40 && parentBg.g < 40 && parentBg.b < 40)) {
              bgOut = {r:9, g:13, b:53, a:bg.a};
            }
          }
        }
      }
      setImportant(st, 'background-color', cssColor(bgOut));
    }
  }

  function restore(state) {
    if (!state || !state.touched) return;
    state.touched.forEach(function(el) {
      if (!el || !(ORIGINAL_STYLE in el)) return;
      const original = el[ORIGINAL_STYLE];
      if (original === null || original === undefined) el.removeAttribute('style');
      else el.setAttribute('style', original);
      try { delete el[ORIGINAL_STYLE]; } catch (_) { el[ORIGINAL_STYLE] = undefined; }
    });
    state.touched.clear();
  }

  function applyAll(state) {
    if (!document.documentElement) return;
    installGlobalStyle();
    applyElement(document.documentElement, state);
    if (document.body) applyElement(document.body, state);
    document.querySelectorAll('*').forEach(function(el) { applyElement(el, state); });
  }

  function schedule(state) {
    if (state.pending) return;
    state.pending = true;
    requestAnimationFrame(function() {
      state.pending = false;
      if (state.enabled) applyAll(state);
    });
  }

  function enable() {
    let state = window[STATE_KEY];
    if (!state) state = window[STATE_KEY] = {enabled:false, touched:new Set(), observer:null, pending:false};
    state.enabled = true;
    applyAll(state);
    signalReady();
    if (!state.observer) {
      state.observer = new MutationObserver(function() { schedule(state); });
      state.observer.observe(document.documentElement || document, {
        childList: true,
        subtree: true,
        attributes: true,
        attributeFilter: ['class', 'data-no-shader']
      });
    }
  }

  function disable() {
    const state = window[STATE_KEY];
    if (!state) { removeGlobalStyle(); return; }
    state.enabled = false;
    if (state.observer) { state.observer.disconnect(); state.observer = null; }
    removeGlobalStyle();
    restore(state);
    signalReady();
  }

  window.__vimbrowserSetElementShader = function(next) { next ? enable() : disable(); };
  if (enabled) enable(); else disable();
})(__VIMBROWSER_SHADER_ENABLED__);
)VIMSHADER";
}

}  // namespace vimbrowser
