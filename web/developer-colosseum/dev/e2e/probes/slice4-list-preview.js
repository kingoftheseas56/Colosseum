(async () => {
  const episodes = document.querySelector('[data-section="episodes"]');
  const rows = [...episodes?.querySelectorAll('.title-unit') || []];
  const next = episodes?.querySelector('.title-unit[data-next-up="true"]');
  if (!rows.length || !next)
    return { ok: false, stage: 'episode-rows', rows: rows.length, next: !!next };
  const board = document.getElementById('board');
  const season = document.querySelector('[data-section="seasons"]');
  board.scrollTop += season.getBoundingClientRect().top - 90;
  await new Promise(resolve => setTimeout(resolve, 300));
  return { ok: true, rows: rows.length, nextUp: next.dataset.unitId,
    season: document.querySelector('[data-key="theatre.season.trigger"]')?.textContent.trim() };
})()
