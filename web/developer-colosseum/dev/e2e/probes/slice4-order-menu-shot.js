(async () => {
  const board = document.getElementById('board');
  const episodes = document.querySelector('[data-section="episodes"]');
  const trigger = episodes?.querySelector('[data-key="theatre.season.trigger"]');
  if (!board || !trigger) return { ok: false, stage: 'picker-missing' };
  board.scrollTop += episodes.getBoundingClientRect().top - 250;
  await new Promise(resolve => setTimeout(resolve, 250));
  trigger.click();
  const menu = document.querySelector('.title-season-menu');
  const modes = [...menu?.querySelectorAll('[data-key^="theatre.season.mode."]') || []]
    .map(node => node.textContent.trim());
  if (!menu || !modes.includes('Aired')) return { ok: false, stage: 'menu-missing', modes };
  const titleId = String(window.CW?.router?.current()?.params?.id || '');
  const isAnime = titleId.startsWith('mal:') || titleId.startsWith('kitsu:');
  if (!isAnime && modes.length !== 1)
    return { ok: false, stage: 'unexpected-tv-order', modes };
  if (isAnime && (modes.length !== 2 || !modes.includes('Absolute')))
    return { ok: false, stage: 'anime-order-missing', modes };
  return { ok: true, modes, rows: episodes.querySelectorAll('.title-unit').length,
    menuRight: Math.round(menu.getBoundingClientRect().right), closeOverlayAfterCapture: true };
})()
