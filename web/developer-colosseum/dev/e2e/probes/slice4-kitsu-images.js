(async () => {
  const section = document.querySelector('[data-section="episodes"]');
  const rows = [...section?.querySelectorAll('.title-unit') || []];
  if (!rows.length) return { ok: false, stage: 'episodes' };
  const board = document.getElementById('board');
  const seasons = document.querySelector('[data-section="seasons"]');
  board.scrollTop += seasons.getBoundingClientRect().top - 90;
  await new Promise(resolve => setTimeout(resolve, 1500));
  const images = rows.slice(0, 8).map(row => {
    const img = row.querySelector('.title-unit-art img');
    return { id: row.dataset.unitId, url: img?.currentSrc || img?.src || '',
      loaded: !!img?.naturalWidth, failed: !!img?.complete && !img?.naturalWidth };
  });
  return { ok: true, rows: rows.length, images };
})()
