(async () => {
  const waitFor = async predicate => {
    for (let i = 0; i < 120; i++) {
      if (predicate()) return true;
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    return false;
  };
  const episodes = document.querySelector('[data-section="episodes"]');
  const board = document.getElementById('board');
  if (!episodes || !board) return { ok: false, stage: 'episodes-missing' };
  board.scrollTop += episodes.getBoundingClientRect().top - 280;
  await new Promise(resolve => setTimeout(resolve, 250));
  let trigger = document.querySelector('[data-key="theatre.season.trigger"]');
  trigger?.click();
  if (!document.querySelector('.title-season-menu')) return { ok: false, stage: 'menu-missing' };
  if (!await waitFor(() => document.querySelector('[data-key="theatre.season.mode.absolute"]')))
    return { ok: false, stage: 'absolute-mapping-unavailable' };
  if (document.querySelector('[data-key="theatre.season.mode.absolute"].on')) {
    document.querySelector('[data-key="theatre.season.mode.seasons"]').click();
    if (!await waitFor(() => document.querySelector('[data-key="theatre.season.trigger"]')
      ?.textContent.includes('Season')))
      return { ok: false, stage: 'aired-mode-stale' };
    trigger = document.querySelector('[data-key="theatre.season.trigger"]');
    trigger.click();
  }
  if (!await waitFor(() => document.querySelectorAll('.title-season-option').length >= 20))
    return { ok: false, stage: 'aired-rows-stale' };
  const menu = document.querySelector('.title-season-menu');
  const modes = [...menu?.querySelectorAll('[data-key^="theatre.season.mode."]') || []]
    .map(node => node.textContent.trim());
  const rows = menu?.querySelectorAll('.title-season-option').length || 0;
  if (!menu || modes.join(',') !== 'Aired,Absolute' || rows < 20)
    return { ok: false, stage: 'aired-options', modes, rows };
  return { ok: true, modes, rows,
    menuHeight: Math.round(menu.getBoundingClientRect().height), closeOverlayAfterCapture: true };
})()
