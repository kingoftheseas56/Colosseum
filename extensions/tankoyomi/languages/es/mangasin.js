(function (root, factory) {
  const provider = factory();
  if (typeof module === 'object' && module.exports) module.exports = provider;
  else root.TankoyomiProvider = provider;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const BASE = 'https://m440.in';
  // Public page script /js/ads2.js, observed 2026-10-22. Rotate in a provider
  // update if the site rotates its chapter-data key; never execute site JS.
  const CHAPTER_KEY = 'X^Ib1O*HLVh%3W2t';
  const need = (ctx, method) => {
    if (!ctx || typeof ctx[method] !== 'function')
      throw new Error('Tankoyomi runtime missing ' + method + '()');
    return ctx[method].bind(ctx);
  };
  const seriesUrl = url => /^https:\/\/m440\.in\/manga\/[a-z0-9][a-z0-9-]*\/?$/i.test(url);
  const chapterUrl = url => /^https:\/\/m440\.in\/manga\/[a-z0-9][a-z0-9-]*\/[a-z0-9][a-z0-9-]*\/?$/i.test(url);
  const slugOk = value => /^[a-z0-9][a-z0-9-]*$/i.test(String(value || ''));
  const numberOf = value => {
    const n = Number(value);
    return String(value ?? '').trim() !== '' && Number.isFinite(n) ? n : null;
  };

  function searchSeries(ctx, query) {
    const search = String(query || '').trim();
    if (!search) return [];
    return need(ctx, 'fetchJson')(BASE + '/search?q=' + encodeURIComponent(search)).then(rows => {
      const out = [], seen = new Set();
      for (const row of (Array.isArray(rows) ? rows : [])) {
        const slug = String(row.data || '').trim();
        const title = String(row.value || '').trim();
        if (!slugOk(slug) || !title || title.length > 200 || seen.has(slug)) continue;
        seen.add(slug);
        out.push({
          id: slug, title, url: BASE + '/manga/' + slug,
          source: 'mangasin', language: 'es', aliases: []
        });
      }
      return out;
    });
  }

  function decodeChapters(ctx, html) {
    const match = String(html || '').match(/\b(?:const|let|var)\s+UsaPoncho\s*=\s*("(?:\\.|[^"\\])*")\s*;/);
    if (!match) throw new Error('MangaSin chapter data is missing');
    let envelope;
    try {
      envelope = JSON.parse(JSON.parse(match[1]));
    } catch (_) {
      throw new Error('MangaSin chapter envelope is malformed');
    }
    if (!envelope || typeof envelope.ct !== 'string' || typeof envelope.s !== 'string')
      throw new Error('MangaSin chapter envelope is invalid');

    const plaintext = need(ctx, 'decryptCryptoJs')(envelope.ct, envelope.s, CHAPTER_KEY);
    if (!plaintext) throw new Error('MangaSin chapter data could not be decrypted');
    let rows;
    try {
      rows = JSON.parse(plaintext);
      if (typeof rows === 'string') rows = JSON.parse(rows);
    } catch (_) {
      throw new Error('MangaSin chapter data is not valid JSON');
    }
    if (!Array.isArray(rows) || rows.length > 10000)
      throw new Error('MangaSin chapter data has an invalid shape');
    return rows;
  }

  function getChapters(ctx, series) {
    const url = String(typeof series === 'string' ? series : series && series.url || '').replace(/\/$/, '');
    if (!seriesUrl(url)) throw new Error('MangaSin series URL is invalid');
    return need(ctx, 'fetchText')(url).then(html => {
      const out = [], seen = new Set();
      for (const row of decodeChapters(ctx, html)) {
        const slug = String(row.slug || '').trim();
        if (!slugOk(slug) || seen.has(slug)) continue;
        seen.add(slug);
        const number = numberOf(row.number);
        const title = String(row.name || '').trim().slice(0, 200)
          || (number === null ? 'Capítulo' : 'Capítulo ' + row.number);
        out.push({
          id: String(row.id || slug), title, number,
          url: url + '/' + slug, source: 'mangasin', language: 'es'
        });
      }
      return out.sort((a, b) =>
        (a.number == null ? 1e12 : a.number) - (b.number == null ? 1e12 : b.number));
    });
  }

  function attr(tag, name) {
    const re = new RegExp('(?:^|\\s)' + name + '\\s*=\\s*(?:"([^"]*)"|\\x27([^\\x27]*)\\x27)', 'i');
    const match = tag.match(re);
    return match ? (match[1] == null ? match[2] : match[1]) : '';
  }
  function imageUrl(ctx, candidate) {
    let value = String(candidate || '').trim().replace(/&amp;/gi, '&');
    // Older chapters (for example One Piece 1–3) put Base64 of a
    // percent-encoded Blogspot URL after a fake "https://" prefix.
    // Decode only this bounded, site-observed wrapper; never execute page JS.
    const packed = /^https:\/\/([A-Za-z0-9+\/=_-]{32,8192})$/.exec(value);
    if (packed) {
      try { value = decodeURIComponent(need(ctx, 'decodeBase64')(packed[1])); }
      catch (_) { return ''; }
    } else if (value.startsWith('//')) value = 'https:' + value;
    else if (value.startsWith('/')) value = BASE + value;
    // Media domains are deliberately narrower than the reader's public-HTTPS
    // transport policy; the transport also rejects private DNS and redirects.
    if (!/^https:\/\/(?:(?:[a-z0-9-]+\.)*m440\.in|[1-4]\.bp\.blogspot\.com)\/[^\s"'<>]+$/i.test(value)
        || /\/images\/loading\.gif(?:[?#]|$)/i.test(value)) return '';
    return value;
  }

  function getPages(ctx, chapter) {
    const url = String(typeof chapter === 'string' ? chapter : chapter && chapter.url || '');
    if (!chapterUrl(url)) throw new Error('MangaSin chapter URL is invalid');
    return need(ctx, 'fetchText')(url).then(html => {
      const section = String(html || '').match(/<div\b[^>]*\bid\s*=\s*(["'])all\1[^>]*>([\s\S]*?)<\/div>/i);
      if (!section) return [];
      const out = [], seen = new Set();
      const images = section[2].match(/<img\b[^>]*>/gi) || [];
      for (const image of images) {
        if (!/\bimg-responsive\b/i.test(attr(image, 'class'))) continue;
        const raw = attr(image, 'data-src') || attr(image, 'data-lazy-src')
          || attr(image, 'data-cfsrc') || attr(image, 'src');
        const resolved = imageUrl(ctx, raw);
        if (!resolved || seen.has(resolved)) continue;
        seen.add(resolved);
        out.push({ index: out.length, url: resolved, referer: url });
      }
      return out;
    });
  }

  return Object.freeze({
    id: 'mangasin', name: 'MangaSin', language: 'es', baseUrl: BASE,
    searchSeries, getChapters, getPages
  });
});
