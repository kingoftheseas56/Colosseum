(function (root, factory) {
  const provider = factory();
  if (typeof module === 'object' && module.exports) module.exports = provider;
  else root.TankoyomiProvider = provider;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const BASE = 'https://sushiscan.fr';
  const PREFIX = '/catalogue/';
  const need = function(ctx, name) {
    if (!ctx || typeof ctx[name] !== 'function') throw new Error('Tankoyomi runtime missing ' + name + '()');
    return ctx[name].bind(ctx);
  };
  const decode = function(value) {
    return String(value || '').replace(/&nbsp;/gi, ' ').replace(/&amp;/gi, '&')
      .replace(/&quot;/gi, '"').replace(/&#39;|&apos;/gi, "'")
      .replace(/&lt;/gi, '<').replace(/&gt;/gi, '>');
  };
  const strip = function(value) {
    return decode(String(value || '').replace(/<script\b[\s\S]*?<\/script>/gi, ' ')
      .replace(/<style\b[\s\S]*?<\/style>/gi, ' ').replace(/<[^>]+>/g, ' ')
      .replace(/\s+/g, ' ').trim());
  };
  const attr = function(markup, name) {
    const re = new RegExp('\\b' + name + '\\s*=\\s*(["\\\'])([\\s\\S]*?)\\1', 'i');
    const m = String(markup || '').match(re);
    return m ? decode(m[2]) : '';
  };
  const absolute = function(value, base) {
    const raw = decode(value).trim();
    if (!raw) return '';
    if (/^https:\/\//i.test(raw)) return raw;
    if (/^\/\//.test(raw)) return 'https:' + raw;
    if (raw.charAt(0) === '/') return BASE + raw;
    const clean = String(base || BASE).split('#')[0].split('?')[0];
    const slash = clean.lastIndexOf('/');
    return (slash >= 8 ? clean.slice(0, slash + 1) : clean + '/') + raw;
  };
  const slug = function(url) {
    const parts = String(url || '').split('?')[0].split('/').filter(function(x) { return x.length; });
    return parts.length ? parts[parts.length - 1] : String(url || '');
  };
  const normalized = function(value) {
    return strip(value).normalize('NFD').replace(/[\u0300-\u036f]/g, '')
      .toLowerCase().replace(/[^a-z0-9]+/g, ' ').trim();
  };
  const chapterNumber = function(label, url) {
    const named = String(label || '').match(/(?:chapitre|chapter|ch\.?)[^\d]*([\d]+(?:[.,]\d+)?)/i);
    const tail = String(url || '').match(/(?:chapitre[-_/]?|\/)(\d+(?:[.-]\d+)?)(?:\/|$)/i);
    const raw = named ? named[1] : (tail ? tail[1] : '');
    return Number(String(raw).replace(',', '.')) || null;
  };
  const elementById = function(html, id) {
    const open = new RegExp('<([a-z0-9:-]+)\\b[^>]*\\bid=["\\\']' + id + '["\\\'][^>]*>', 'i').exec(html);
    if (!open) return '';
    const tag = open[1], token = new RegExp('<\\/?' + tag + '\\b[^>]*>', 'gi');
    token.lastIndex = open.index; let depth = 0, m;
    while ((m = token.exec(html))) { if (/^<\//.test(m[0])) depth--; else depth++; if (depth === 0) return html.slice(open.index, token.lastIndex); }
    return html.slice(open.index);
  };
  function searchSeries(ctx, title) {
    const query = String(title || '').trim();
    if (!query) return [];
    const fetchText = need(ctx, 'fetchText');
    const url = BASE + PREFIX + '?title=' + encodeURIComponent(query) + '&page=1';
    return fetchText(url, { headers: { Referer: BASE + '/' } }).then(function(html) {
      const out = [], seen = {}, wanted = normalized(query).split(' ')[0];
      const re = /<a\b([^>]*)>([\s\S]*?)<\/a>/gi; let m;
      while ((m = re.exec(html))) {
        const href = absolute(attr(m[1], 'href'), BASE);
        if (!href || href.indexOf(BASE + PREFIX) !== 0 || href === BASE + PREFIX) continue;
        const name = attr(m[1], 'title') || strip(m[2]);
        if (!name || (wanted && normalized(name).indexOf(wanted) < 0) || seen[href]) continue;
        seen[href] = true;
        out.push({ id: slug(href), title: name, url: href, thumbnail: null, source: 'sushiscan-fr', language: 'fr' });
      }
      return out;
    });
  }

  function getChapters(ctx, series) {
    const fetchText = need(ctx, 'fetchText');
    const seriesUrl = absolute(series.url, BASE);
    if (!seriesUrl) throw new Error('Sushiscan.fr series URL missing');
    return fetchText(seriesUrl, { headers: { Referer: BASE + '/' }, timeoutMs: 45000 }).then(function(html) {
      const block = elementById(html, 'chapterlist') || html;
      const out = [], seen = {}, re = /<a\b([^>]*)>([\s\S]*?)<\/a>/gi; let m;
      while ((m = re.exec(block))) {
        const href = absolute(attr(m[1], 'href'), seriesUrl), label = strip(m[2]);
        if (!href || !/(?:chapitre|chapter)/i.test(label) || seen[href]) continue;
        seen[href] = true;
        out.push({ id: slug(href), seriesId: series.id || slug(seriesUrl), number: chapterNumber(label, href), label, title: null, url: href, source: 'sushiscan-fr', language: 'fr' });
      }
      return out;
    });
  }

  function getPages(ctx, chapter) {
    const fetchText = need(ctx, 'fetchText');
    const chapterUrl = absolute(chapter.url, BASE);
    if (!chapterUrl) throw new Error('Sushiscan.fr chapter URL missing');
    return fetchText(chapterUrl, { headers: { Referer: chapterUrl }, timeoutMs: 45000 }).then(function(html) {
      const block = elementById(html, 'readerarea') || html;
      const out = [], seen = {}, re = /<img\b([^>]*)>/gi; let m;
      while ((m = re.exec(block))) {
        let src = attr(m[1], 'srcset');
        if (src) src = src.split(/\s+/)[0];
        src = src || attr(m[1], 'data-cfsrc') || attr(m[1], 'data-src') || attr(m[1], 'data-lazy-src') || attr(m[1], 'src');
        const url = absolute(src, chapterUrl);
        if (!url || seen[url]) continue;
        seen[url] = true;
        out.push({ index: out.length, url, referer: chapterUrl });
      }
      return out;
    });
  }

  return Object.freeze({
    id: 'sushiscan-fr', name: 'Sushiscan.fr', language: 'fr', baseUrl: BASE,
    searchSeries, getChapters, getPages
  });
});