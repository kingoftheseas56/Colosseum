(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.Tankoyomi = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  // Mirrors TankoyomiProviderRegistry: normalization preserves the complete
  // regional tag, and resolving a request to an installed language is a
  // separate decision (exact code, explicit alias, unique base-language match).
  function normalizeLanguage(code) {
    return String(code || '').trim().toLowerCase().replace(/_/g, '-');
  }

  function languageConfig(manifest, code) {
    const wanted = normalizeLanguage(code);
    return (manifest.languages || []).find(x => normalizeLanguage(x.code) === wanted) || null;
  }

  function resolveLanguage(manifest, requested) {
    const raw = String(requested || '').trim();
    const tag = raw ? normalizeLanguage(raw) : normalizeLanguage(manifest.defaultLanguage || 'en');
    const languages = (manifest && manifest.languages) || [];
    if (!tag) return null;
    const exact = languages.find(x => normalizeLanguage(x.code) === tag);
    if (exact) return normalizeLanguage(exact.code);
    const byAlias = languages.filter(x => (x.aliases || []).some(a => normalizeLanguage(a) === tag));
    if (byAlias.length === 1) return normalizeLanguage(byAlias[0].code);
    if (byAlias.length > 1) return null;
    const base = tag.split('-')[0];
    if (!base) return null;
    const byBase = languages.filter(x => normalizeLanguage(x.code).split('-')[0] === base);
    if (byBase.length === 1) return normalizeLanguage(byBase[0].code);
    return null;
  }

  function providersForLanguage(manifest, requested) {
    const language = languageConfig(manifest, resolveLanguage(manifest, requested) || '');
    if (!language) return [];
    return (language.providers || [])      .filter(x => x.enabled !== false)
      .slice()
      .sort((a, b) => Number(a.priority || 999) - Number(b.priority || 999));
  }

  function providerEntry(manifest, requested, providerId) {
    return providersForLanguage(manifest, requested)
      .find(x => x.id === providerId) || null;
  }

  return Object.freeze({
    contractVersion: 1,
    normalizeLanguage,
    languageConfig,
    resolveLanguage,
    providersForLanguage,
    providerEntry
  });
});
