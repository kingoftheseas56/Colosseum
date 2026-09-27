(async () => {
  document.querySelector('[data-key="theatre.season.trigger"]')?.click();
  if (document.querySelector('[data-key="theatre.season.mode.absolute"].on'))
    document.querySelector('[data-key="theatre.season.mode.seasons"]')?.click();
  for (let i = 0; i < 100 && !document.querySelector('[data-key="theatre.season.trigger"]'); i++)
    await new Promise(resolve => setTimeout(resolve, 100));
  document.querySelector('[data-key="theatre.season.trigger"]')?.click();
  const season2 = document.querySelector('[data-key="theatre.season.2"]');
  if (!season2) return { ok: false, stage: 'season-two' };
  season2.click();
  for (let i = 0; i < 100; i++) {
    if (document.querySelector('[data-key="theatre.season.trigger"]')?.textContent.includes('Season 2')) break;
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  const board = document.getElementById('board');
  const seasons = document.querySelector('[data-section="seasons"]');
  board.scrollTop += seasons.getBoundingClientRect().top - 90;
  await new Promise(resolve => setTimeout(resolve, 1500));
  const rows = [...document.querySelectorAll('.title-unit')];
  return { ok: rows.length > 0, season: 2, rows: rows.length,
    images: rows.slice(0, 3).map(row => {
      const image = row.querySelector('.title-unit-art img');
      return { id: row.dataset.unitId, url: image?.currentSrc || image?.src || '',
        loaded: !!image?.naturalWidth, failed: !!image?.complete && !image?.naturalWidth };
    }) };
})()
