/* Downloads return link. No account state, storage, redirects or app launch. */
'use strict';
(() => {
  if (location.pathname !== '/downloads') return;
  const values = new URLSearchParams(location.search).getAll('collection');
  if (values.length !== 1 || !/^[A-Za-z0-9_-]{43}$/.test(values[0])) return;
  const panel = document.getElementById('collection-return');
  if (!panel) return;
  const link = `https://getcolosseum.world/c/${values[0]}`;
  const returnLink = document.getElementById('collection-return-link');
  const copy = document.getElementById('collection-return-copy');
  const fallback = document.getElementById('collection-return-fallback');
  const input = document.getElementById('collection-return-url');
  const status = document.getElementById('collection-return-status');
  returnLink.href = link;
  input.value = link;
  panel.hidden = false;
  copy.addEventListener('click', async () => {
    try {
      if (!navigator.clipboard?.writeText) throw new Error('clipboard_unavailable');
      await navigator.clipboard.writeText(link);
      fallback.hidden = true;
      status.textContent = 'Collection link copied. Return to it after installing Colosseum.';
    } catch (_) {
      fallback.hidden = false;
      input.focus(); input.select();
      status.textContent = 'Select and copy the collection link below.';
    }
  });
})();
