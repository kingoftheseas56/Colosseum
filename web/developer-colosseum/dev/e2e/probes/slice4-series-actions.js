(async () => {
  const waitFor = async predicate => {
    for (let i = 0; i < 180; i++) {
      if (predicate()) return true;
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    return false;
  };
  document.querySelector('[data-key="theatre.season.trigger"]')?.click();
  if (document.querySelector('[data-key="theatre.season.mode.absolute"]'))
    return { ok: false, stage: 'tv-absolute-visible' };
  document.querySelector('.title-season-menu')?.__close?.();
  const next = document.querySelector('.title-unit[data-next-up="true"]');
  const play = next?.querySelector('[data-key$=".play"]');
  if (!play) return { ok: false, stage: 'next-up-missing' };
  const episodeId = next.dataset.unitId;
  play.focus();
  play.click();
  if (!await waitFor(() => document.querySelector('.title-source-sheet')?.dataset.targetId === episodeId))
    return { ok: false, stage: 'sources-target', episodeId };
  document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true, cancelable: true }));
  if (!await waitFor(() => !document.querySelector('.title-source-sheet')))
    return { ok: false, stage: 'sources-escape' };
  if (document.activeElement?.getAttribute('data-key') !== play.getAttribute('data-key'))
    return { ok: false, stage: 'focus-restore', focus: document.activeElement?.getAttribute('data-key') };
  document.querySelector('[data-key="theatre.season.trigger"]')?.click();
  document.querySelector('[data-key="theatre.season.2"]')?.click();
  if (!await waitFor(() => document.querySelector('[data-key="theatre.season.trigger"]')?.textContent.includes('Season 2')))
    return { ok: false, stage: 'season-switch' };
  return { ok: true, sourceTarget: episodeId, season: 2,
    firstEpisode: document.querySelector('.title-unit')?.dataset.unitId };
})()
