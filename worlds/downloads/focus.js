export function installSpatialFocus(onBack) {
  const selector = '[data-focus]:not([disabled])';

  function candidates() {
    return [...document.querySelectorAll(selector)].filter(el => {
      const r = el.getBoundingClientRect();
      return r.width > 0 && r.height > 0;
    });
  }

  function move(direction) {
    const items = candidates();
    if (!items.length) return;
    const current = document.activeElement && document.activeElement.matches?.(selector)
      ? document.activeElement : items[0];
    if (current === items[0] && document.activeElement !== current) {
      current.focus();
      return;
    }

    const a = current.getBoundingClientRect();
    const ax = a.left + a.width / 2;
    const ay = a.top + a.height / 2;
    let best = null;
    let bestScore = Infinity;

    for (const item of items) {
      if (item === current) continue;
      const b = item.getBoundingClientRect();
      const bx = b.left + b.width / 2;
      const by = b.top + b.height / 2;
      const dx = bx - ax;
      const dy = by - ay;
      const primary = direction === 'left' ? -dx
        : direction === 'right' ? dx
        : direction === 'up' ? -dy : dy;
      if (primary <= 2) continue;
      const secondary = direction === 'left' || direction === 'right' ? Math.abs(dy) : Math.abs(dx);
      const score = primary + secondary * 2.4;
      if (score < bestScore) {
        bestScore = score;
        best = item;
      }
    }
    if (best) {
      best.focus();
      best.scrollIntoView({ block: 'nearest', inline: 'nearest', behavior: 'smooth' });
    }
  }

  function keydown(event) {
    if (event.key === 'Escape') {
      if (onBack && onBack()) event.preventDefault();
      return;
    }
    const map = { ArrowLeft: 'left', ArrowRight: 'right', ArrowUp: 'up', ArrowDown: 'down' };
    if (!map[event.key]) return;
    if (document.activeElement?.matches?.('input, textarea, select')) return;
    event.preventDefault();
    move(map[event.key]);
  }

  document.addEventListener('keydown', keydown);
  requestAnimationFrame(() => candidates()[0]?.focus());
  return () => document.removeEventListener('keydown', keydown);
}