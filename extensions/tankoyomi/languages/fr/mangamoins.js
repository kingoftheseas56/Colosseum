(function (root, factory) {
  const provider = factory();
  if (typeof module === 'object' && module.exports) module.exports = provider;
  else root.TankoyomiProvider = provider;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const BASE = 'https://mangamoins.com';
  const API = BASE + '/api/v1';
  let warmed = false;
  let salts = [];
  let saltsAt = 0;
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
    return decode(String(value || '').replace(/<[^>]+>/g, ' ').replace(/\s+/g, ' ').trim());
  };
  const slugOf = function(value) {
    return String(value || '').normalize('NFD').replace(/[\u0300-\u036f]/g, '')
      .toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '');
  };
  const queryUrl = function(path, params) {
    let url = API + '/' + path, first = true;
    Object.keys(params || {}).forEach(function(key) {
      url += (first ? '?' : '&') + encodeURIComponent(key) + '=' + encodeURIComponent(String(params[key])); first = false;
    });
    return url;
  };
  function api(ctx, path, params) {
    const fetchText = need(ctx, 'fetchText');
    const fetchJson = need(ctx, 'fetchJson');
    const url = queryUrl(path, params);
    const options = { headers: { Referer: BASE + '/', Origin: BASE, Accept: 'application/json' }, timeoutMs: 45000 };
    function request() {
      return fetchJson(url, options).catch(function() {
        warmed = false;
        return fetchText(BASE, { headers: { Referer: BASE + '/' }, timeoutMs: 45000 }).then(function() {
          warmed = true;
          return fetchJson(url, options);
        });
      });
    }
    if (warmed) return request();
    return fetchText(BASE, { headers: { Referer: BASE + '/' }, timeoutMs: 45000 }).then(function() {
      warmed = true;
      return request();
    });
  }

  function searchSeries(ctx, title) {
    const query = String(title || '').trim();
    if (!query) return [];
    return api(ctx, 'explore', { page: 1, limit: 20, q: query }).then(function(data) {
      const rows = data && data.data ? data.data : [];
      return rows.map(function(item) {
        const id = item.slug || slugOf(item.title);
        return { id, title: strip(item.title), url: BASE + '/manga/' + id, thumbnail: item.cover || null, source: 'mangamoins', language: 'fr' };
      });
    });
  }
  function getChapters(ctx, series) {
    const id = series.id || String(series.url || '').split('/manga/')[1] || '';
    if (!id) throw new Error('MangaMoins series id missing');
    return api(ctx, 'manga', { manga: id }).then(function(data) {
      const rows = data && data.chapters ? data.chapters : [];
      return rows.map(function(ch) {
        const number = Number(ch.num);
        const chapterText = isFinite(number) && Math.floor(number) === number ? String(number) : String(ch.num || '');
        const baseLabel = 'Chapitre ' + chapterText;
        const title = strip(ch.title || '');
        return {
          id: ch.slug, seriesId: id, number: isFinite(number) ? number : null,
          label: title && title.toLowerCase() !== baseLabel.toLowerCase() ? baseLabel + ' - ' + title : baseLabel,
          title: title || null, url: BASE + '/scan/' + ch.slug, source: 'mangamoins', language: 'fr'
        };
      });
    });
  }

  function parseSalts(script, pathSegment) {
    const out = [], seen = {}, re = /['"]([^'"]*)['"]/g; let m;
    while ((m = re.exec(String(script || '')))) {
      const value = m[1].replace(/\\x([a-f\d]{2})/gi, function(_, n) { return String.fromCharCode(parseInt(n, 16)); });
      if (value.length >= 3 && pathSegment.indexOf(value) >= 0 && !seen[value]) { seen[value] = true; out.push(value); }
    }
    out.sort(function(a, b) { return b.length - a.length; });
    return out;
  }

  function buildPages(data, activeSalts, chapterUrl) {
    let root = String(data.pagesBaseUrl || '').replace(/\/$/, '').replace(/_b$/, '');
    (activeSalts || []).forEach(function(salt) { root = root.split(salt).join(''); });
    const count = Number(data.pageNumbers || 0), out = [];
    for (let i = 1; i <= count; ++i) out.push({ index: i - 1, url: root + '/' + (i < 10 ? '0' + i : String(i)) + '.webp', referer: chapterUrl });
    return out;
  }
  function getPages(ctx, chapter) {
    const fetchText = need(ctx, 'fetchText');
    const chapterId = chapter.id || String(chapter.url || '').split('/scan/')[1] || '';
    if (!chapterId) throw new Error('MangaMoins chapter id missing');
    const chapterUrl = chapter.url || BASE + '/scan/' + chapterId;
    return api(ctx, 'scan', { slug: chapterId }).then(function(data) {
      const pagesBaseUrl = String(data.pagesBaseUrl || '');
      if (!pagesBaseUrl) return [];
      if (salts.length && Date.now() - saltsAt < 10800000) return buildPages(data, salts, chapterUrl);
      const pathParts = pagesBaseUrl.replace(/\/$/, '').split('/');
      const pathSegment = pathParts[pathParts.length - 1] || '';
      return fetchText(BASE + '/includes/components/js/reader.js', { headers: { Referer: BASE + '/' }, timeoutMs: 45000 })
        .then(function(script) {
          const found = parseSalts(script, pathSegment);
          salts = found.length ? found : ['a1f', 'Z0_9']; saltsAt = Date.now();
          return buildPages(data, salts, chapterUrl);
        }).catch(function() {
          const fallback = salts.length ? salts : ['a1f', 'Z0_9'];
          return buildPages(data, fallback, chapterUrl);
        });
    });
  }

  return Object.freeze({
    id: 'mangamoins', name: 'MangaMoins', language: 'fr', baseUrl: BASE,
    searchSeries, getChapters, getPages
  });
});