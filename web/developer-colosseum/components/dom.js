// components/dom.js — tiny element builder + the image-with-monogram-fallback rule (CONTRACT §4.6).
(function (CW) {
  'use strict';

  /** h('button.pc', {data-focus:'', onclick}, child, 'text', [more]) */
  function h(sel, attrs, ...children) {
    const [tag, ...classes] = sel.split('.');
    const el = document.createElement(tag || 'div');
    if (classes.length) el.className = classes.join(' ');
    for (const [k, v] of Object.entries(attrs || {})) {
      if (v == null || v === false) continue;
      if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
      else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
      else if (k === 'text') el.textContent = v;
      else el.setAttribute(k, v === true ? '' : v);
    }
    append(el, children);
    return el;
  }

  function append(el, children) {
    for (const c of children.flat(Infinity)) {
      if (c == null || c === false) continue;
      el.appendChild(c instanceof Node ? c : document.createTextNode(String(c)));
    }
  }

  /** A face: the image fades in on load; on error (or no URL) the title monogram stays. Never a broken image. */
  function face(url, title) {
    const fb = h('span.fb', {}, title || '');
    const el = h('span.face', {}, fb);
    if (url) {
      const img = h('img', { alt: '', loading: 'lazy', decoding: 'async', src: url });
      img.addEventListener('load', () => { img.classList.add('on'); fb.remove(); });
      img.addEventListener('error', () => img.remove());
      el.appendChild(img);
    }
    return el;
  }

  CW.h = h;
  CW.face = face;
})(window.CW = window.CW || {});
