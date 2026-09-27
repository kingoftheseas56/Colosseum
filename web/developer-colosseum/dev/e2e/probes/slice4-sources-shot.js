(async () => {
  const next = document.querySelector('.title-unit[data-next-up="true"]');
  const play = next?.querySelector('[data-key$=".play"]');
  if (!play) return { ok: false, stage: 'next-up' };
  const board = document.getElementById('board');
  board.scrollTop += next.getBoundingClientRect().top - 180;
  play.focus();
  play.click();
  for (let i = 0; i < 110; i++) {
    const sheet = document.querySelector('.title-source-sheet');
    if (sheet?.dataset.targetId === next.dataset.unitId)
      return { ok: true, target: sheet.dataset.targetId, rows: sheet.querySelectorAll('.title-source-row').length,
        closeOverlayAfterCapture: true };
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  return { ok: false, stage: 'source-sheet' };
})()
