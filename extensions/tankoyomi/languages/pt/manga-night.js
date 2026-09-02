(function (root, factory) {
  const provider = factory();
  if (typeof module === 'object' && module.exports) module.exports = provider;
  else root.TankoyomiProvider = provider;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const BASE = 'https://www.manganight.com.br';
  const MDEX = 'https://api.mangadex.org';
  const need = (ctx, name) => {
    if (!ctx || typeof ctx[name] !== 'function') throw new Error(`Tankoyomi runtime missing ${name}()`);
    return ctx[name].bind(ctx);
  };
  const decode = value => String(value || '')
    .replace(/&amp;/g, '&').replace(/&quot;/g, '"').replace(/&#39;/g, "'")
    .replace(/&lt;/g, '<').replace(/&gt;/g, '>');
  const strip = value => decode(String(value || '').replace(/<[^>]+>/g, ' ').replace(/\s+/g, ' ').trim());
  function directSlug(value) {
    return String(value || '').normalize('NFD').replace(/[\u0300-\u036f]/g, '')
      .toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '');
  }

  function searchSeries(ctx, title) {
    const cleanTitle = String(title || '').trim();
    const slug = directSlug(cleanTitle);
    if (!slug) return [];
    return [{
      id: slug, title: cleanTitle, url: `${BASE}/manga/${slug}`,
      thumbnail: null, source: 'manga-night', language: 'pt'
    }];
  }

  function getChapters(ctx, series) {
    const fetchText = need(ctx, 'fetchText');
    const seriesUrl = series.url || `${BASE}/manga/${series.id}`;
    const seriesId = series.id || (String(seriesUrl).split('/manga/')[1] || '').split(/[/?#]/)[0];
    if (!seriesId) throw new Error('Manga Night series id missing');
    return fetchText(seriesUrl, { timeoutMs: 45000 }).then(function(body) {
      const seen = new Set(), chapters = [];
      const hrefRe = /href="(\/manga\/[^"\/]+\/capitulo\/(\d+))"/gi;
      let m;
      while ((m = hrefRe.exec(body))) {
        if (seen.has(m[2])) continue;
        const anchorEnd = body.indexOf('</a>', m.index);
        const stop = anchorEnd >= m.index && anchorEnd - m.index < 8192 ? anchorEnd + 4 : m.index + 8192;
        const segment = body.slice(m.index, stop);
        const pStart = segment.indexOf('<p');
        const pEnd = pStart >= 0 ? segment.indexOf('</p>', pStart) : -1;
        const label = strip(pStart >= 0 && pEnd > pStart ? segment.slice(pStart, pEnd + 4) : segment);
        const numberMatch = label.match(/Cap[ií]tulo\s+([0-9]+(?:\.[0-9]+)?)/i);
        if (!numberMatch) continue;
        seen.add(m[2]);
        chapters.push({
          id: m[2], seriesId, number: Number(numberMatch[1]), label, title: null,
          url: `${BASE}${m[1]}`, source: 'manga-night', language: 'pt'
        });
      }
      return chapters.sort((a, b) => a.number - b.number);
    });
  }

  function getPages(ctx, chapter) {
    const fetchText = need(ctx, 'fetchText');
    const fetchJson = need(ctx, 'fetchJson');
    const chapterUrl = chapter.url || `${BASE}/manga/${chapter.seriesId}/capitulo/${chapter.id}`;
    return fetchText(chapterUrl, { timeoutMs: 45000 }).then(function(body) {
      const refs = [], ids = [], seenIds = new Set();
      const re = /(\/api\/reader\/image\/[A-Za-z0-9._~!$&'()*+,;=:@%\/-]+)/g;
      let m;
      while ((m = re.exec(body))) {
        const match = m[1].match(/^\/api\/reader\/image\/mdex\/([0-9a-f-]{36})\/([^/?#]+)$/i);
        const ref = { path: m[1], chapterId: match ? match[1] : '', filename: match ? match[2] : '' };
        refs.push(ref);
        if (ref.chapterId && !seenIds.has(ref.chapterId)) {
          seenIds.add(ref.chapterId);
          ids.push(ref.chapterId);
        }
      }
      if (!refs.length) throw new Error('Manga Night page payload not found');
      const homes = {};

      function loadHome(index) {
        if (index >= ids.length) return build();
        const id = ids[index];
        return fetchJson(`${MDEX}/at-home/server/${id}`).then(function(home) {
          homes[id] = home;
          return loadHome(index + 1);
        });
      }
      function build() {
        return refs.map(function(ref, index) {
          const home = ref.chapterId ? homes[ref.chapterId] : null;
          if (home && ref.filename && home.chapter) {
            const full = home.chapter.data || [];
            const saver = home.chapter.dataSaver || [];
            if (full.indexOf(ref.filename) >= 0)
              return { index, url: `${home.baseUrl}/data/${home.chapter.hash}/${ref.filename}` };
            if (saver.indexOf(ref.filename) >= 0)
              return { index, url: `${home.baseUrl}/data-saver/${home.chapter.hash}/${ref.filename}` };
          }
          return { index, url: `${BASE}${ref.path}`, referer: chapterUrl };
        });
      }
      return loadHome(0);
    });
  }

  return Object.freeze({
    id: 'manga-night', name: 'Manga Night', language: 'pt', baseUrl: BASE,
    searchSeries, getChapters, getPages
  });
});
