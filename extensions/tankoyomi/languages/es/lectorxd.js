(function (root, factory) {
  const provider = factory();
  if (typeof module === 'object' && module.exports) module.exports = provider;
  else root.TankoyomiProvider = provider;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const BASE = 'https://lectorxd.com';
  const IMAGE_HOSTS = new Set(['s1.cdnlxd.xyz', 's2.cdnlxd.xyz']);
  const SERIES_PATH_RE = /^\/(?:manga|manhwa|manhua|novela)\/[^/]+\/?$/i;
  const CHAPTER_PATH_RE = /^\/(?:manga|manhwa|manhua|novela)\/[^/]+\/leer\/[^/]+\/?$/i;
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
    const re = new RegExp('(?:^|\\s)' + name + '\\s*=\\s*(["\\\'])([\\s\\S]*?)\\1', 'i');
    const m = String(markup || '').match(re);
    return m ? decode(m[2]).trim() : '';
  };
  function localUrl(value, base) {
    const raw = decode(value).trim();
    if (!raw) return null;
    let rest = raw, path = '', hostname = 'lectorxd.com';
    if (/^https:\/\//i.test(rest)) {
      const absolute = rest.match(/^https:\/\/([^/?#]+)([\s\S]*)$/i);
      if (!absolute) return null;
      hostname = absolute[1].toLowerCase();
      rest = absolute[2] || '/';
    } else if (/^http:\/\//i.test(rest)) {
      return null;
    } else if (/^\/\//.test(rest)) {
      const absolute = ('https:' + rest).match(/^https:\/\/([^/?#]+)([\s\S]*)$/i);
      if (!absolute) return null;
      hostname = absolute[1].toLowerCase();
      rest = absolute[2] || '/';
    } else if (rest.charAt(0) !== '/') {
      const baseMatch = String(base || BASE + '/').match(/^https:\/\/lectorxd\.com(\/[^?#]*)?/i);
      const basePath = baseMatch ? (baseMatch[1] || '/') : '/';
      const slash = basePath.lastIndexOf('/');
      rest = basePath.slice(0, slash + 1) + rest;
    }
    if (hostname !== 'lectorxd.com') return null;
    const suffixAt = rest.search(/[?#]/);
    path = suffixAt < 0 ? rest : rest.slice(0, suffixAt);
    const suffix = suffixAt < 0 ? '' : rest.slice(suffixAt);
    if (!path) path = '/';
    return { hostname, pathname: path, href: BASE + path + suffix };
  }
  function pageImageUrl(value, base) {
    let raw = decode(value).trim();
    if (!raw) return '';
    if (/^\/\//.test(raw)) raw = 'https:' + raw;
    else if (raw.charAt(0) === '/') raw = BASE + raw;
    else if (!/^https:\/\//i.test(raw)) {
      const chapterPath = String(base || BASE + '/').replace(/[?#].*$/, '');
      const slash = chapterPath.lastIndexOf('/');
      raw = chapterPath.slice(0, slash + 1) + raw;
    }
    const absolute = raw.match(/^https:\/\/([^/?#]+)([\s\S]*)$/i);
    if (!absolute) return '';
    const hostname = absolute[1].toLowerCase();
    if (!IMAGE_HOSTS.has(hostname)) return '';
    return 'https://' + hostname + (absolute[2] || '/');
  }
  function seriesInfo(series) {
    const rawUrl = typeof series === 'string' ? series : (series && series.url) || '';
    const rawId = series && typeof series === 'object' ? series.id : '';
    const url = localUrl(rawUrl || rawId, BASE + '/');
    if (!url || !SERIES_PATH_RE.test(url.pathname)) throw new Error('LectorXD series URL is missing or invalid');
    return { id: url.pathname, url: BASE + url.pathname };
  }
  function chapterInfo(chapter) {
    const rawUrl = typeof chapter === 'string' ? chapter : (chapter && chapter.url) || '';
    const rawId = chapter && typeof chapter === 'object' ? chapter.id : '';
    const url = localUrl(rawUrl || rawId, BASE + '/');
    if (!url || !CHAPTER_PATH_RE.test(url.pathname)) throw new Error('LectorXD chapter URL is missing or invalid');
    const path = url.pathname;
    const seriesPath = path.replace(/\/leer\/[^/]+\/?$/i, '');
    const suppliedSeries = chapter && typeof chapter === 'object' ? (chapter.seriesUrl || chapter.seriesId) : '';
    const seriesUrl = suppliedSeries ? localUrl(suppliedSeries, BASE + '/') : localUrl(seriesPath, BASE + '/');
    return {
      id: path,
      url: BASE + path,
      seriesId: seriesUrl && SERIES_PATH_RE.test(seriesUrl.pathname) ? seriesUrl.pathname : seriesPath,
      seriesUrl: seriesUrl && SERIES_PATH_RE.test(seriesUrl.pathname) ? BASE + seriesUrl.pathname : BASE + seriesPath
    };
  }
  function chapterNumber(label) {
    const match = String(label || '').match(/\bcap(?:[ií]tulo)?\.?\s*([0-9]+(?:[.,][0-9]+)?)/i);
    if (!match) return null;
    const number = Number(match[1].replace(',', '.'));
    return Number.isFinite(number) ? number : null;
  }

  function searchSeries(ctx, query) {
    if (!String(query || '').trim()) return Promise.resolve([]);
    // The pinned public HaruNeko LectorXD adapter supports catalogue pagination
    // and direct URLs, but defines no title-search route. Do not invent one.
    return Promise.reject(new Error('LectorXD title search is unsupported until a verified search route is available'));
  }

  function getChapters(ctx, series) {
    const fetchText = need(ctx, 'fetchText');
    const info = seriesInfo(series);
    return fetchText(info.url, { headers: { Referer: BASE + '/' } }).then(function(html) {
      const out = [], seen = new Set();
      const anchorRe = /<a\b([^>]*)>([\s\S]*?)<\/a>/gi;
      let match;
      while ((match = anchorRe.exec(String(html || '')))) {
        const href = attr(match[1], 'href');
        const link = localUrl(href, info.url);
        if (!link || !CHAPTER_PATH_RE.test(link.pathname)) continue;
        const chapterPath = link.pathname;
        if (!chapterPath.toLowerCase().startsWith(info.id.toLowerCase().replace(/\/$/, '') + '/leer/')) continue;
        if (seen.has(chapterPath)) continue;
        const heading = match[2].match(/<h4\b[^>]*>([\s\S]*?)<\/h4>/i);
        const title = strip(heading ? heading[1] : match[2]);
        const number = chapterNumber(title);
        if (!title || number == null) continue;
        seen.add(chapterPath);
        out.push({
          id: chapterPath,
          seriesId: info.id,
          seriesUrl: info.url,
          number,
          title,
          label: title,
          url: BASE + chapterPath,
          source: 'lectorxd',
          language: 'es'
        });
      }
      return out.sort(function(a, b) { return a.number - b.number; });
    });
  }

  function readerBlock(html) {
    const open = /<([a-z][a-z0-9:-]*)\b[^>]*\bid=["']reader-content["'][^>]*>/i.exec(String(html || ''));
    if (!open) return '';
    const tag = open[1];
    const token = new RegExp('<\\/?' + tag + '\\b[^>]*>', 'gi');
    token.lastIndex = open.index;
    let depth = 0, match;
    while ((match = token.exec(String(html || '')))) {
      if (/^<\//.test(match[0])) depth--;
      else if (!/\/\s*>$/.test(match[0])) depth++;
      if (depth === 0) return String(html).slice(open.index, token.lastIndex);
    }
    return String(html).slice(open.index);
  }

  function getPages(ctx, chapter) {
    const fetchText = need(ctx, 'fetchText');
    const info = chapterInfo(chapter);
    return fetchText(info.url, { headers: { Referer: info.seriesUrl } }).then(function(html) {
      const block = readerBlock(html);
      if (!block) throw new Error('LectorXD reader content was not found');
      const out = [], seen = new Set();
      const imageRe = /<img\b([^>]*)>/gi;
      let match;
      while ((match = imageRe.exec(block))) {
        const classes = attr(match[1], 'class').split(/\s+/);
        if (classes.indexOf('page-image') < 0) continue;
        const rawUrl = attr(match[1], 'data-src') || attr(match[1], 'src');
        if (!rawUrl) continue;
        const resolved = pageImageUrl(rawUrl, info.url);
        if (!resolved) continue;
        if (seen.has(resolved)) continue;
        seen.add(resolved);
        out.push({ index: out.length, url: resolved, referer: info.url });
      }
      if (!out.length) throw new Error('LectorXD page images were not found');
      return out;
    });
  }

  return Object.freeze({
    id: 'lectorxd',
    name: 'LectorXD',
    language: 'es',
    baseUrl: BASE,
    searchSeries,
    getChapters,
    getPages
  });
});
