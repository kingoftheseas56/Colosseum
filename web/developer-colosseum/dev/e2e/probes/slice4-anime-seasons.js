(async () => {
  let trigger = document.querySelector('[data-key="theatre.season.trigger"]');
  if (!trigger) return { ok: false, stage: 'long-season-dropdown' };
  trigger.focus();
  trigger.click();
  const absoluteAvailable = !!document.querySelector('[data-key="theatre.season.mode.absolute"]');
  if (document.querySelector('[data-key="theatre.season.mode.absolute"].on')) {
    document.querySelector('[data-key="theatre.season.mode.seasons"]').click();
    for (let i = 0; i < 100 && !document.querySelector('[data-key="theatre.season.trigger"]')?.textContent.includes('Season'); i++)
      await new Promise(resolve => setTimeout(resolve, 100));
    trigger = document.querySelector('[data-key="theatre.season.trigger"]');
    trigger.click();
  }
  const options = document.querySelectorAll('.title-season-option');
  if (options.length <= 10) return { ok: false, stage: 'dropdown-options', count: options.length };
  const second = document.querySelector('[data-key="theatre.season.2"]');
  if (!second) return { ok: false, stage: 'season-two-missing' };
  second.click();
  for (let i = 0; i < 100; i++) {
    if (document.querySelector('[data-key="theatre.season.trigger"]')?.textContent.includes('Season 2')) {
      const board = document.getElementById('board');
      const season = document.querySelector('[data-section="seasons"]');
      board.scrollTop += season.getBoundingClientRect().top - 90;
      await new Promise(resolve => setTimeout(resolve, 250));
      return { ok: true, options: options.length, season: 2,
        absoluteAvailable,
        menuClosed: !document.querySelector('.title-season-menu') };
    }
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  return { ok: false, stage: 'season-switch' };
})()
