(function (root, factory) {
  const provider = factory();
  if (typeof module === 'object' && module.exports) module.exports = provider;
  else root.TankoyomiProvider = provider;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const BASE = 'https://mangaonline.green';
  const need = (ctx, name) => {
    if (!ctx || typeof ctx[name] !== 'function') throw new Error(`Tankoyomi runtime missing ${name}()`);
    return ctx[name].bind(ctx);
  };
  const decode = value => String(value || '')
    .replace(/&amp;/g, '&').replace(/&quot;/g, '"').replace(/&#39;/g, "'")
    .replace(/&lt;/g, '<').replace(/&gt;/g, '>');
  const strip = value => decode(String(value || '').replace(/<[^>]+>/g, ' ').replace(/\s+/g, ' ').trim());

  function absolute(value) {
    const url = decode(value);
    if (/^https?:\/\//i.test(url)) return url;
    return BASE + (url.charAt(0) === '/' ? url : '/' + url);
  }
  function pathId(url) {
    return String(url || '').replace(BASE, '').replace(/^\/+|\/+$/g, '');
  }
  function cards(body) {
    const out = [], seen = new Set();
    const re = /<a\b[^>]*href="([^"]*\/manga\/[^"]+)"[^>]*>([\s\S]*?<h3[^>]*>[\s\S]*?<\/h3>[\s\S]*?)<\/a>/gi;
    let m;
    while ((m = re.exec(body))) {
      const titleMatch = m[2].match(/<h3[^>]*>([\s\S]*?)<\/h3>/i);
      const title = strip(titleMatch ? titleMatch[1] : '');
      if (!title) continue;
      const url = absolute(m[1]);
      if (url.indexOf(BASE) !== 0 || seen.has(url)) continue;
      seen.add(url);
      out.push({ id: pathId(url), title, url, thumbnail: null, source: 'manga-online', language: 'pt' });
    }
    return out;
  }

  function searchSeries(ctx, title) {
    const fetchText = need(ctx, 'fetchText');
    return fetchText(`${BASE}/catalogo?perPage=24&page=1&q=${encodeURIComponent(title)}`).then(cards);
  }

  function getChapters(ctx, series) {
    const fetchText = need(ctx, 'fetchText');
    const seriesUrl = series.url || `${BASE}/${series.id}`;
    return fetchText(seriesUrl).then(function(body) {
      const seriesId = series.id || pathId(seriesUrl);
      const chapters = [], seen = new Set();
      const rowRe = /<[^>]*class="[^"]*chapter-row[^"]*"[^>]*>([\s\S]*?)(?=<[^>]*class="[^"]*chapter-row|$)/gi;
      let row;
      while ((row = rowRe.exec(body))) {
        const href = row[1].match(/class="[^"]*chapter-main-link[^"]*"[^>]*href="([^"]+)"|href="([^"]+)"[^>]*class="[^"]*chapter-main-link/i);
        const titleMatch = row[1].match(/class="[^"]*chapter-title-line[^"]*"[^>]*>([\s\S]*?)<\//i);
        const urlPart = href ? (href[1] || href[2]) : '';
        if (!urlPart) continue;
        const url = absolute(urlPart);
        if (seen.has(url)) continue;
        seen.add(url);
        const label = strip(titleMatch ? titleMatch[1] : '') || 'Capítulo';
        const n = label.match(/([0-9]+(?:\.[0-9]+)?)/);
        chapters.push({
          id: pathId(url), seriesId,
          number: n ? Number(n[1]) : null,
          label, title: null, url,
          source: 'manga-online', language: 'pt'
        });
      }
      return chapters;
    });
  }

  function getPages(ctx, chapter) {
    const fetchText = need(ctx, 'fetchText');
    return fetchText(chapter.url).then(function(body) {
      const marker = body.search(/class="[^"]*reader-content[^"]*"/i);
      const region = marker >= 0 ? body.slice(marker, marker + 2000000) : body;
      const urls = [], seen = new Set();
      const re = /<img\b[^>]*(?:src|data-src)="([^"]+)"[^>]*>/gi;
      let m;
      while ((m = re.exec(region))) {
        const url = absolute(m[1]);
        if (/logo|avatar|icon|cover/i.test(url) || seen.has(url)) continue;
        seen.add(url);
        urls.push(url);
      }
      if (!urls.length) throw new Error('Manga Online page images not found');
      return urls.map((url, index) => ({ index, url, referer: chapter.url }));
    });
  }

  return Object.freeze({
    id: 'manga-online', name: 'Manga Online', language: 'pt', baseUrl: BASE,
    searchSeries, getChapters, getPages
  });
});
