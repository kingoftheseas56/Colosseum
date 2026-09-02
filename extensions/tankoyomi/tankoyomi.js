(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.Tankoyomi = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  function normalizeLanguage(code) {
    return String(code || '').trim().toLowerCase().replace('_', '-').split('-')[0];
  }

  function languageConfig(manifest, code) {
    const wanted = normalizeLanguage(code);
    return (manifest.languages || []).find(x => normalizeLanguage(x.code) === wanted) || null;
  }

  function resolveLanguage(manifest, requested) {
    const raw = String(requested || '').trim();
    if (!raw) return normalizeLanguage(manifest.defaultLanguage || 'en');
    return normalizeLanguage(raw);
  }

  function providersForLanguage(manifest, requested) {
    const language = languageConfig(manifest, resolveLanguage(manifest, requested));
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
