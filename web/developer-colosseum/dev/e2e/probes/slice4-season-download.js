(async () => {
  const waitFor = async predicate => {
    for (let i = 0; i < 100; i++) {
      if (predicate()) return true;
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    return false;
  };
  const button = document.querySelector('[data-key="theatre.season.download"]');
  if (!button) return { ok: false, stage: 'season-download-control' };
  button.focus();
  button.click();
  if (!await waitFor(() => !!document.querySelector('[data-key="theatre.season.auto"]')))
    return { ok: false, stage: 'season-source-sheet' };
  document.querySelector('[data-key="theatre.season.auto"]').click();
  if (!await waitFor(() => !document.querySelector('.title-source-sheet')))
    return { ok: false, stage: 'season-queue-action' };
  if (!await waitFor(() => [...document.querySelectorAll('.title-unit-meta')]
    .some(el => /Download (queued|failed|\d+%)/.test(el.textContent))))
    return { ok: false, stage: 'download-state-not-projected' };
  return { ok: true, season: 1, rows: document.querySelectorAll('.title-unit').length,
    states: [...document.querySelectorAll('.title-unit-meta')]
      .map(el => el.textContent).filter(text => text.includes('Download')).slice(0, 3) };
})()
