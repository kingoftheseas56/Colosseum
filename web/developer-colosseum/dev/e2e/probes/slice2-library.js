(async () => {
  const id = 'tt0133093';
  const stale = await CW.env.act('detail.theatre.markWatched', { id: 'tt0000000', watched: true });
  if (stale?.ok !== false || !/no longer open/i.test(stale.error || ''))
    return { ok: false, stage: 'stale-detail-identity', stale };
  const saved = await CW.env.act('detail.theatre.collection', {
    id, saved: true, title: 'The Matrix', cover: '', notify: false
  });
  if (!saved?.ok) return { ok: false, stage: 'save', saved };
  CW.router.go({ name: 'world', world: 'Theatre', tab: 'library' });
  const until = async predicate => {
    for (let i = 0; i < 80; i++) {
      const value = predicate();
      if (value) return value;
      await new Promise(resolve => setTimeout(resolve, 100));
    }
    return null;
  };
  const card = await until(() => document.querySelector('[data-section="theatre.library"] .pcm'));
  if (!card) return { ok: false, stage: 'library-card' };
  card.click();
  const mark = [...document.querySelectorAll('.cardmenu [role="menuitem"]')]
    .find(item => item.textContent.trim() === 'Mark watched');
  if (!mark) return { ok: false, stage: 'card-menu-mark' };
  mark.click();
  const labels = await until(() => {
    const updatedCard = document.querySelector('[data-section="theatre.library"] .pcm');
    if (!updatedCard) return null;
    updatedCard.click();
    const current = [...document.querySelectorAll('.cardmenu [role="menuitem"]')]
      .map(item => item.textContent.trim());
    document.querySelector('.cardmenu')?.__close?.();
    return current.includes('Mark unwatched') ? current : null;
  });
  return { ok: !!labels, stage: 'card-menu-toggle', staleRejected: true, labels,
    toast: document.querySelector('#toast')?.textContent };
})()
