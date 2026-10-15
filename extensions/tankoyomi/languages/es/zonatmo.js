(function (root, factory) {
  const provider = factory();
  if (typeof module === 'object' && module.exports) module.exports = provider;
  else root.TankoyomiProvider = provider;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';
  const BASE = 'https://zonatmo.org';
  const need = (ctx, name) => {
    if (!ctx || typeof ctx[name] !== 'function') throw new Error(`Tankoyomi runtime missing ${name}()`);
    return ctx[name].bind(ctx);
  };
  const num = value => {
    const m = String(value || '').match(/\d+(?:\.\d+)?/);
    return m ? Number(m[0]) : null;
  };

  function searchSeries(ctx, query) {
    const fetchJson = need(ctx, 'fetchJson');
    return fetchJson(`${BASE}/api/search/suggest?q=${encodeURIComponent(query)}`).then(function(rows) {
      return (Array.isArray(rows) ? rows : [])
        .filter(x => ['manga', 'manhwa', 'manhua'].includes(String(x.type || '').toLowerCase()))
        .map(x => ({
          id: String(x.id), title: String(x.title || ''), url: String(x.url || ''),
          source: 'zonatmo', language: 'es', aliases: [], authors: x.authors || []
        }));
    });
  }
  function getChapters(ctx, series) {
    const fetchText = need(ctx, 'fetchText');
    const url = typeof series === 'string' ? series : series.url;
    return fetchText(url).then(function(html) {
      const out = [];
      const rowRe = /<li\b[^>]*data-chapter-number="([^"]+)"[^>]*>([\s\S]*?)<\/li>/gi;
      let row;
      while ((row = rowRe.exec(html))) {
        const upload = row[2].match(/href="(https?:\/\/zonatmo\.org\/view_uploads\/(\d+))"/i);
        if (!upload) continue;
        const number = num(row[1]);
        out.push({
          id: upload[2], title: `Capítulo ${row[1]}`, number,
          url: upload[1], source: 'zonatmo', language: 'es'
        });
      }
      return out.sort((a, b) =>
        (a.number == null ? 1e12 : a.number) - (b.number == null ? 1e12 : b.number));
    });
  }

  function getPages(ctx, chapter) {
    const fetchText = need(ctx, 'fetchText');
    const url = typeof chapter === 'string' && /^https?:/i.test(chapter)
      ? chapter : (chapter.url || `${BASE}/view_uploads/${chapter.id || chapter}`);
    return fetchText(url).then(function(html) {
      const out = [], seen = new Set();
      const re = /https?:\/\/storage\d*\.zonatmo\.org\/chapters\/[^"'\s<>]+?\.(?:png|jpe?g|webp)(?:\?[^"'\s<>]*)?/gi;
      let m, index = 0;
      while ((m = re.exec(html))) {
        if (seen.has(m[0])) continue;
        seen.add(m[0]);
        out.push({ index: index++, url: m[0] });
      }
      return out;
    });
  }

  return Object.freeze({
    id: 'zonatmo',
    name: 'ZonaTMO',
    language: 'es',
    baseUrl: BASE,
    searchSeries,
    getChapters,
    getPages
  });
});
