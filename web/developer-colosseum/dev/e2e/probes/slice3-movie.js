(async () => {
  const providers = [...document.querySelectorAll('.title-score')].map(el => el.dataset.provider);
  const play = document.querySelector('[data-key="theatre.play"]')?.textContent.trim();
  if (providers.join(',') !== 'imdb' || !/(Play|Resume)/.test(play || ''))
    return { ok: false, stage: 'hero-data', providers, play };
  const waitFor = async predicate => {
    for (let i = 0; i < 80; i++) {
      if (predicate()) return true;
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    return false;
  };
  document.querySelector('[data-key="theatre.collection"]')?.click();
  if (!await waitFor(() => document.querySelector('[data-key="theatre.collection"]')
    ?.getAttribute('aria-pressed') === 'true'))
    return { ok: false, stage: 'library-toggle', providers, play };
  document.querySelector('[data-key="theatre.markWatched"]')?.click();
  if (!await waitFor(() => document.querySelector('[data-key="theatre.markWatched"]')
    ?.getAttribute('aria-pressed') === 'true'))
    return { ok: false, stage: 'watched-toggle', providers, play };
  return { ok: true, providers, play, librarySaved: true, watchedMarked: true };
})()
