// Remote/keyboard: arrows move to the nearest [data-focus] control in that direction; Enter activates;
// Escape/Backspace leaves the world. Mouse and Tab still work as normal.
export function installSpatialFocus(onBack) {
  const all = () => [...document.querySelectorAll('[data-focus]')].filter(e => !e.disabled && e.offsetParent !== null);
  const center = r => ({ x: r.left + r.width / 2, y: r.top + r.height / 2 });
  document.addEventListener('keydown', e => {
    const typing = e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA');
    if (e.key === 'Escape' && typing) { e.target.blur(); return; }
    if (e.key === 'Escape' || (e.key === 'Backspace' && !typing)) { e.preventDefault(); onBack(); return; }
    if (typing && (e.key === 'ArrowLeft' || e.key === 'ArrowRight' || (e.target.tagName === 'SELECT' && (e.key === 'ArrowUp' || e.key === 'ArrowDown')))) return;
    const dir = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[e.key];
    if (!dir) return;
    e.preventDefault();
    const items = all();
    const cur = document.activeElement && items.includes(document.activeElement) ? document.activeElement : null;
    if (!cur) { items[0] && items[0].focus(); return; }
    const c = center(cur.getBoundingClientRect());
    let best = null, bestScore = Infinity;
    for (const el of items) {
      if (el === cur) continue;
      const p = center(el.getBoundingClientRect());
      const dx = p.x - c.x, dy = p.y - c.y;
      const along = dx * dir[0] + dy * dir[1];
      if (along <= 1) continue;
      const across = Math.abs(dx * dir[1]) + Math.abs(dy * dir[0]);
      const score = along + across * 2.5;
      if (score < bestScore) { bestScore = score; best = el; }
    }
    if (best) { best.focus(); best.scrollIntoView({ block: 'nearest', behavior: 'smooth' }); }
  });
}
