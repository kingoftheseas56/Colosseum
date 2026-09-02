(function (root, factory) {
  const provider = factory();
  if (typeof module === 'object' && module.exports) module.exports = provider;
  else root.TankoyomiProvider = provider;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';
  const BASE = 'https://es.niadd.com';
  const strip = s => String(s || '').replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ').trim();
  const num = s => {
    const m = String(s || '').match(/(\d+(?:\.\d+)?)/);
    return m ? Number(m[1]) : null;
  };
  const need = (ctx, name) => {
    if (!ctx || typeof ctx[name] !== 'function') throw new Error(`Tankoyomi runtime missing ${name}()`);
    return ctx[name].bind(ctx);
  };

  function searchSeries(ctx, query) {
    const fetchText = need(ctx, 'fetchText');
    return fetchText(`${BASE}/search/?name=${encodeURIComponent(query)}`).then(function(html) {
      const out = [], seen = new Set();
      const re = /<a\b[^>]*href="(https?:\/\/es\.niadd\.com\/manga\/[^\"]+\.html)"[^>]*>([\s\S]*?)<\/a>/gi;
      let m;
      while ((m = re.exec(html))) {
        const title = strip(m[2]);
        if (!title || title.length > 180 || seen.has(m[1])) continue;
        seen.add(m[1]);
        out.push({ id: m[1].split('/').pop().replace(/\.html$/i, ''), title, url: m[1], source: 'niadd', language: 'es' });
      }
      return out;
    });
  }
  function getChapters(ctx, series) {
    const fetchText = need(ctx, 'fetchText');
    const seriesUrl = typeof series === 'string' ? series : series.url;
    const chaptersUrl = seriesUrl.replace(/\.html$/i, '/chapters.html');
    return fetchText(chaptersUrl).then(function(html) {
      const out = [], seen = new Set();
      const re = /<a\b[^>]*href="(https?:\/\/es\.niadd\.com\/chapter\/[^\"]+)"[^>]*(?:title="([^"]*)")?[^>]*>([\s\S]*?)<\/a>/gi;
      let m;
      while ((m = re.exec(html))) {
        const url = m[1];
        if (seen.has(url)) continue;
        const title = strip(m[2] || m[3]);
        if (!title) continue;
        seen.add(url);
        const id = (url.match(/\/(\d+)(?:[-/]?\d*)?\/?(?:\.html)?$/) || [])[1] || url;
        out.push({ id, title, number: num(title), url, source: 'niadd', language: 'es' });
      }
      return out.sort((a, b) => (a.number == null ? 1e12 : a.number) - (b.number == null ? 1e12 : b.number));
    });
  }

  function readerImage(html) {
    const urls = String(html || '').match(/https?:\/\/[^"'\s<>]*movietop\.cc\/[^"'\s<>]+?\.(?:png|jpe?g|webp)(?:\?[^"'\s<>]*)?/gi) || [];
    return urls[0] || '';
  }
  function getPages(ctx, chapter) {
    const fetchText = need(ctx, 'fetchText');
    const firstUrl = typeof chapter === 'string' ? chapter : chapter.url;
    return fetchText(firstUrl).then(function(firstHtml) {
      const pageUrls = [firstUrl];
      const seenPages = new Set(pageUrls);
      const pageRe = /(?:value|option_val)="(https?:\/\/es\.niadd\.com\/chapter\/[^\"]+?(?:-\d+)?\.html)"/gi;
      let m;
      while ((m = pageRe.exec(firstHtml))) {
        if (!seenPages.has(m[1])) { seenPages.add(m[1]); pageUrls.push(m[1]); }
      }
      const out = [], seenImages = new Set();
      function collect(html) {
        const image = readerImage(html);
        if (!image || seenImages.has(image)) return;
        seenImages.add(image);
        out.push({ index: out.length, url: image });
      }
      function loadAt(index) {
        if (index >= pageUrls.length) return out;
        if (index === 0) {
          collect(firstHtml);
          return loadAt(1);
        }
        return fetchText(pageUrls[index]).then(function(html) {
          collect(html);
          return loadAt(index + 1);
        });
      }
      return loadAt(0);
    });
  }
  return Object.freeze({
    id: 'niadd', name: 'NiAdd', language: 'es', baseUrl: BASE,
    searchSeries, getChapters, getPages
  });
});
