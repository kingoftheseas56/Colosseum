(function (root, factory) {
  const provider = factory();
  if (typeof module === 'object' && module.exports) module.exports = provider;
  else root.TankoyomiProvider = provider;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';
  const BASE = 'https://weebcentral.com';
  const strip = s => String(s || '')
    .replace(/<svg\b[^>]*>[\s\S]*?<\/svg>/gi, ' ')
    .replace(/<style\b[^>]*>[\s\S]*?<\/style>/gi, ' ')
    .replace(/<time\b[^>]*>[\s\S]*?<\/time>/gi, ' ')
    .replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ').trim();
  const numberOf = s => {
    const m = String(s || '').match(/(\d+(?:\.\d+)?)/);
    return m ? Number(m[1]) : null;
  };
  const need = (ctx, name) => {
    if (!ctx || typeof ctx[name] !== 'function') throw new Error(`Tankoyomi runtime missing ${name}()`);
    return ctx[name].bind(ctx);
  };

  function searchSeries(ctx, query) {
    const fetchText = need(ctx, 'fetchText');
    const url = `${BASE}/search/data?text=${encodeURIComponent(query)}&sort=Best%20Match&order=Descending&official=Any&display_mode=Full%20Display`;
    return fetchText(url, { headers: { 'HX-Request': 'true', 'HX-Target': 'search-results' } }).then(function(html) {
      const out = [], seen = new Set();
      const re = /<a\b[^>]*href="(?:https?:\/\/[^/\"]+)?\/series\/([^/\"]+)\/[^\"]*"[^>]*>([\s\S]*?)<\/a>/gi;
      let m;
      while ((m = re.exec(html))) {
        if (seen.has(m[1])) continue;
        const title = strip(m[2]);
        if (!title) continue;
        seen.add(m[1]);
        out.push({ id: m[1], title, url: `${BASE}/series/${m[1]}`, source: 'weebcentral', language: 'en' });
      }
      return out;
    });
  }
  function getChapters(ctx, series) {
    const fetchText = need(ctx, 'fetchText');
    const id = typeof series === 'string' ? series : series.id;
    return fetchText(`${BASE}/series/${encodeURIComponent(id)}/full-chapter-list`).then(function(html) {
      const out = [];
      const re = /<a\s+href="(?:https?:\/\/[^/\"]+)?\/chapters\/([^\"]+)"[^>]*>([\s\S]*?)<\/a>/gi;
      let m;
      while ((m = re.exec(html))) {
        const title = strip(m[2]).replace(/\bLast Read\b/gi, '').trim();
        out.push({ id: m[1], title, number: numberOf(title), url: `${BASE}/chapters/${m[1]}`, source: 'weebcentral', language: 'en' });
      }
      return out.sort((a, b) => (a.number == null ? 1e12 : a.number) - (b.number == null ? 1e12 : b.number));
    });
  }

  function getPages(ctx, chapter) {
    const fetchText = need(ctx, 'fetchText');
    const id = typeof chapter === 'string' ? chapter : chapter.id;
    const url = `${BASE}/chapters/${encodeURIComponent(id)}/images?is_prev=False&current_page=1&reading_style=long_strip`;
    return fetchText(url).then(function(html) {
      const out = [], seen = new Set();
      const re = /https?:\/\/[^"'\s<>]+?\.(?:png|jpe?g|webp)(?:\?[^"'\s<>]*)?/gi;
      let m, index = 0;
      while ((m = re.exec(html))) {
        if (/\/broken_image\./i.test(m[0]) || seen.has(m[0])) continue;
        seen.add(m[0]);
        out.push({ index: index++, url: m[0] });
      }
      return out;
    });
  }
  return Object.freeze({
    id: 'weebcentral',
    name: 'WeebCentral',
    language: 'en',
    baseUrl: BASE,
    searchSeries,
    getChapters,
    getPages
  });
});
