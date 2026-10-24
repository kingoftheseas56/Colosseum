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
  const MAX_ASTRO_PROPS_LENGTH = 1024 * 1024;
  const MAX_ASTRO_JSON_DEPTH = 32;
  const MAX_ASTRO_CHAPTERS = 8000;
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
  const decodeAttribute = function(value) {
    const entities = {
      '&nbsp;': ' ', '&amp;': '&', '&quot;': '"', '&#39;': "'",
      '&apos;': "'", '&lt;': '<', '&gt;': '>'
    };
    return String(value || '').replace(/&(?:nbsp|amp|quot|#39|apos|lt|gt);/gi, function(entity) {
      return entities[entity.toLowerCase()] || entity;
    });
  };
  const attribute = function(markup, name) {
    const re = new RegExp('(?:^|\\s)' + name + '\\s*=\\s*(["\\\'])([\\s\\S]*?)\\1', 'i');
    const m = String(markup || '').match(re);
    return m ? decodeAttribute(m[2]).trim() : null;
  };
  const attr = function(markup, name) {
    return attribute(markup, name) || '';
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

  function invalidAstroProps(detail) {
    return new Error('LectorXD Astro chapter props are invalid' + (detail ? ': ' + detail : ''));
  }

  function hasBoundedJsonDepth(value) {
    let depth = 0, inString = false, escaped = false;
    for (let i = 0; i < value.length; i++) {
      const ch = value.charAt(i);
      if (inString) {
        if (escaped) escaped = false;
        else if (ch === '\\') escaped = true;
        else if (ch === '"') inString = false;
        continue;
      }
      if (ch === '"') inString = true;
      else if (ch === '{' || ch === '[') {
        depth++;
        if (depth > MAX_ASTRO_JSON_DEPTH) return false;
      } else if (ch === '}' || ch === ']') {
        depth--;
        if (depth < 0) return false;
      }
    }
    return !inString && depth === 0;
  }

  function isRecord(value) {
    return value !== null && typeof value === 'object' && !Array.isArray(value);
  }

  function astroTupleValue(tuple, type, hasValue) {
    if (!Array.isArray(tuple) || tuple.length < 1 || tuple.length > 2 || tuple[0] !== type)
      throw invalidAstroProps('unexpected Astro tuple');
    if (hasValue && tuple.length !== 2) throw invalidAstroProps('Astro tuple value is missing');
    return tuple.length === 2 ? tuple[1] : undefined;
  }

  function astroChapterValues(propsText) {
    // Astro 7.0.4 commit 5978993531ba3f02a4c639d223f810e80a1fa7d3
    // serializes props as per-field [tag, value] tuples. Read only chapters,
    // each row object, and its primitive chapter field; unrelated props may
    // use other Astro tags and are intentionally left untouched.
    if (propsText == null || !propsText) throw invalidAstroProps('ChapterList props are missing');
    if (propsText.length > MAX_ASTRO_PROPS_LENGTH || !hasBoundedJsonDepth(propsText))
      throw invalidAstroProps('props exceed parser limits');

    let props;
    try {
      props = JSON.parse(propsText);
    } catch (_) {
      throw invalidAstroProps('props are not valid JSON');
    }
    if (!isRecord(props)) throw invalidAstroProps('props must be an object');

    if (!Object.prototype.hasOwnProperty.call(props, 'chapters'))
      throw invalidAstroProps('chapters are missing');

    const serializedChapters = props.chapters;
    const chapters = astroTupleValue(serializedChapters, 1, true);
    if (!Array.isArray(chapters) || chapters.length > MAX_ASTRO_CHAPTERS)
      throw invalidAstroProps('chapters must be a bounded array');

    const out = [];
    for (let i = 0; i < chapters.length; i++) {
      const row = astroTupleValue(chapters[i], 0, true);
      if (!isRecord(row)) throw invalidAstroProps('chapter row must be an object');
      if (!Object.prototype.hasOwnProperty.call(row, 'chapter'))
        throw invalidAstroProps('chapter value is missing');

      const chapterTuple = row.chapter;
      if (!Array.isArray(chapterTuple) || chapterTuple.length < 1 || chapterTuple.length > 2 || chapterTuple[0] !== 0)
        throw invalidAstroProps('chapter value has an unsupported Astro type');
      const chapterValue = chapterTuple.length === 2 ? chapterTuple[1] : undefined;
      if (typeof chapterValue !== 'string' && typeof chapterValue !== 'number')
        throw invalidAstroProps('chapter value must be a string or number');
      out.push(chapterValue);
    }
    return out;
  }

  function chapterListIsland(html) {
    const islandRe = /<astro-island\b([^>]*)>/gi;
    let match;
    while ((match = islandRe.exec(String(html || '')))) {
      const componentUrl = attribute(match[1], 'component-url');
      if (componentUrl && componentUrl.indexOf('ChapterList') >= 0) return match[1];
    }
    return null;
  }

  function chapterIdentifier(value) {
    let segment;
    if (typeof value === 'number') {
      if (!Number.isFinite(value) || value < 0) return null;
      segment = String(value);
    } else if (typeof value === 'string') {
      segment = value.trim();
      if (!/^[0-9]+(?:[.,][0-9]+)?$/.test(segment)) return null;
    } else {
      return null;
    }
    const number = Number(segment.replace(',', '.'));
    return Number.isFinite(number) ? { segment, number } : null;
  }

  function astroChapterRows(propsText, info) {
    const chapters = astroChapterValues(propsText);
    const rows = [], seen = new Set();
    const prefix = info.id.replace(/\/$/, '') + '/leer/';
    for (const value of chapters) {
      const identifier = chapterIdentifier(value);
      if (!identifier) throw invalidAstroProps('chapter entry is invalid');
      const chapterPath = prefix + identifier.segment;
      if (!CHAPTER_PATH_RE.test(chapterPath) || !chapterPath.toLowerCase().startsWith(prefix.toLowerCase()))
        throw invalidAstroProps('chapter path is outside the canonical series');
      if (seen.has(chapterPath)) continue;
      seen.add(chapterPath);
      const title = 'Capítulo ' + identifier.segment;
      rows.push({
        id: chapterPath,
        seriesId: info.id,
        seriesUrl: info.url,
        number: identifier.number,
        title,
        label: title,
        url: BASE + chapterPath,
        source: 'lectorxd',
        language: 'es'
      });
    }
    return rows.sort(function(a, b) { return a.number - b.number; });
  }

  function legacyChapterRows(html, info) {
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
  }

  function searchSeries(ctx, query) {
    if (!String(query || '').trim()) return Promise.resolve([]);
    // HaruNeko LectorXD source f00b48e4e2c48252d419e73cb2041baa127a0ca8
    // confirms catalogue pagination and direct URLs, but defines no title-search route.
    return Promise.reject(new Error('LectorXD title search is unsupported until a verified search route is available'));
  }

  function getChapters(ctx, series) {
    const fetchText = need(ctx, 'fetchText');
    const info = seriesInfo(series);
    return fetchText(info.url, { headers: { Referer: BASE + '/' } }).then(function(html) {
      const islandAttributes = chapterListIsland(html);
      if (islandAttributes !== null) {
        return astroChapterRows(attribute(islandAttributes, 'props'), info);
      }
      return legacyChapterRows(html, info);
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
