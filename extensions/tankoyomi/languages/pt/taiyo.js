(function (root, factory) {
  const provider = factory();
  if (typeof module === 'object' && module.exports) module.exports = provider;
  else root.TankoyomiProvider = provider;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const BASE = 'https://taiyo.moe';
  const CDN = 'https://cdn.taiyo.moe/medias';
  const SEARCH = 'https://meilisearch.taiyo.moe/multi-search';
  let bearerToken = '';

  const need = (ctx, name) => {
    if (!ctx || typeof ctx[name] !== 'function') throw new Error(`Tankoyomi runtime missing ${name}()`);
    return ctx[name].bind(ctx);
  };

  function absolute(path) {
    if (/^https?:\/\//i.test(path)) return path;
    return BASE + (String(path).charAt(0) === '/' ? path : '/' + path);
  }

  function discoverToken(ctx) {
    const fetchText = need(ctx, 'fetchText');
    return fetchText(BASE).then(function(home) {
      const scripts = [], re = /<script[^>]+src="([^"]*\/_next\/[^"]+\.js)"/g;
      let m;
      while ((m = re.exec(home))) scripts.push(absolute(m[1]));
      scripts.reverse();

      function inspect(index) {
        if (index >= scripts.length) throw new Error('Taiyō bearer token not found');
        return fetchText(scripts[index]).then(function(js) {
          const match = js.match(/NEXT_PUBLIC_MEILISEARCH_PUBLIC_KEY:\s*"([^"]+)"/);
          if (match) {
            bearerToken = match[1];
            return bearerToken;
          }
          return inspect(index + 1);
        });
      }
      return inspect(0);
    });
  }

  function titleOf(hit) {
    const titles = hit && hit.titles ? hit.titles : [];
    let best = null;
    for (let i = 0; i < titles.length; i++) {
      const row = titles[i] || {};
      if (String(row.language || '').indexOf('en') === 0 && row.title) return row.title;
      if (!best || Number(row.priority || 0) > Number(best.priority || 0)) best = row;
    }
    return best && best.title ? best.title : (hit.name || hit.id);
  }

  function runSearch(ctx, title, key) {
    const fetchJson = need(ctx, 'fetchJson');
    return fetchJson(SEARCH, {
      method: 'POST',
      headers: { 'Authorization': `Bearer ${key}`, 'Content-Type': 'application/json' },
      body: JSON.stringify({ queries: [{ indexUid: 'medias', q: title, filter: ['deletedAt IS NULL'], limit: 21, offset: 0 }] })
    }).then(function(data) {
      const hits = data && data.results && data.results[0] ? (data.results[0].hits || []) : [];
      return hits.map(function(hit) {
        return {
          id: hit.id,
          title: titleOf(hit),
          url: `${BASE}/media/${hit.id}`,
          thumbnail: hit.coverId ? `${BASE}/_next/image?url=${encodeURIComponent(`${CDN}/${hit.id}/covers/${hit.coverId}.jpg`)}&w=256&q=75` : null,
          source: 'taiyo', language: 'pt'
        };
      });
    });
  }

  function searchSeries(ctx, title) {
    if (bearerToken) return runSearch(ctx, title, bearerToken);
    return discoverToken(ctx).then(function(key) { return runSearch(ctx, title, key); });
  }

  function getChapters(ctx, series) {
    const fetchText = need(ctx, 'fetchText');
    const mediaId = series.id || (String(series.url || '').split('/media/')[1] || '').split('/')[0];
    if (!mediaId) throw new Error('Taiyō series id missing');
    const chapters = [];

    function load(page) {
      const input = { '0': { json: { mediaId, page, perPage: 50 } } };
      const url = `${BASE}/api/trpc/chapters.getByMediaId?batch=1&input=${encodeURIComponent(JSON.stringify(input))}`;
      return fetchText(url).then(function(body) {
        const match = body.match(/(\{"chapters".+"totalPages":\d+\})/);
        if (!match) throw new Error('Taiyō chapter payload not found');
        const parsed = JSON.parse(match[1]);
        const rows = parsed.chapters || [];
        for (let i = 0; i < rows.length; i++) {
          const ch = rows[i] || {};
          const number = Number(ch.number);
          chapters.push({
            id: ch.id, seriesId: mediaId,
            number: isFinite(number) ? number : null,
            label: String(ch.title || '').trim() || `Capítulo ${ch.number}`,
            title: ch.title || null,
            url: `${BASE}/chapter/${ch.id}/1`,
            source: 'taiyo', language: 'pt'
          });
        }
        if (page < Number(parsed.totalPages || 1)) return load(page + 1);
        return chapters.sort((a, b) => (a.number == null ? 1e12 : a.number) - (b.number == null ? 1e12 : b.number));
      });
    }
    return load(1);
  }

  function extractEscapedObject(html, key) {
    let at = -1;
    while ((at = html.indexOf(key, at + 1)) >= 0) {
      const brace = html.indexOf('{', at + key.length);
      if (brace < 0 || brace - at > 50) continue;
      let depth = 0;
      for (let i = brace; i < html.length; i++) {
        if (html[i] === '{') depth++;
        else if (html[i] === '}' && --depth === 0) {
          const raw = html.slice(brace, i + 1).replace(/\\"/g, '"').replace(/\\\\/g, '\\');
          try { return JSON.parse(raw); } catch (error) { break; }
        }
      }
    }
    return null;
  }

  function getPages(ctx, chapter) {
    const fetchText = need(ctx, 'fetchText');
    const chapterId = chapter.id || (String(chapter.url || '').split('/chapter/')[1] || '').split('/')[0];
    if (!chapterId) throw new Error('Taiyō chapter id missing');
    return fetchText(`${BASE}/chapter/${chapterId}/1`).then(function(html) {
      const data = extractEscapedObject(html, 'mediaChapter');
      if (!data || !data.media || !data.media.id || !Array.isArray(data.pages))
        throw new Error('Taiyō page payload not found');
      const root = `${CDN}/${data.media.id}/chapters/${data.id}`;
      return data.pages.map(function(page, index) { return { index, url: `${root}/${page.id}.jpg` }; });
    });
  }

  return Object.freeze({
    id: 'taiyo', name: 'Taiyō', language: 'pt', baseUrl: BASE,
    searchSeries, getChapters, getPages
  });
});
