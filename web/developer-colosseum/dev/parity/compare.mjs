export const normal = value => String(value ?? '').replace(/\s+/gu, ' ').trim();

const rect = entry => entry.rect;
const distance = (a, b) => Math.max(
  Math.abs(a.x - b.x), Math.abs(a.y - b.y),
  Math.abs(a.width - b.width), Math.abs(a.height - b.height));

function allowed(allow, kind, text, side, count) {
  return (allow[kind] || []).some(a => a.text === text && a.side === side
    && a.count === count && normal(a.reason) && normal(a.contract));
}

function group(entries, key) {
  const out = new Map();
  for (const entry of entries) {
    const label = normal(entry[key]);
    if (!label) continue;
    if (!out.has(label)) out.set(label, []);
    out.get(label).push(entry);
  }
  return out;
}

// Pair repeated strings by nearest geometry, so two identical card titles cannot
// conceal an extra or missing occurrence on one side.
function pair(left, right) {
  const remainingLeft = [...left], remainingRight = [...right];
  const matched = [];
  while (remainingLeft.length && remainingRight.length) {
    let bestLeft = 0, bestRight = 0;
    for (let i = 0; i < remainingLeft.length; i++)
      for (let j = 0; j < remainingRight.length; j++)
        if (distance(rect(remainingLeft[i]), rect(remainingRight[j]))
          < distance(rect(remainingLeft[bestLeft]), rect(remainingRight[bestRight]))) {
          bestLeft = i; bestRight = j;
        }
    matched.push([remainingLeft.splice(bestLeft, 1)[0], remainingRight.splice(bestRight, 1)[0]]);
  }
  return { matched, leftExtra: remainingLeft, rightExtra: remainingRight };
}

export function compare(qml, web, allow = {}) {
  for (const kind of ['words', 'controls', 'layout']) {
    if (!Array.isArray(allow[kind] || [])) throw new Error(`allow.${kind} must be an array`);
    for (const a of allow[kind] || []) {
      if (!a.reason || !a.contract) throw new Error(`allow.${kind} entries need reason and contract`);
      if (kind !== 'layout' && (!Number.isInteger(a.count) || a.count < 1))
        throw new Error(`allow.${kind} entries need a positive count`);
      if (kind === 'layout' && !(a.maxDelta > 0))
        throw new Error('allow.layout entries need maxDelta');
    }
  }
  const failures = [];
  const wordDiff = [];
  const controlDiff = [];
  const layout = [];
  const ignoredControls = new Set();
  for (const [kind, key, destination] of [['words', 'text', wordDiff], ['controls', 'label', controlDiff]]) {
    const left = group(qml[kind], key), right = group(web[kind], key);
    for (const label of new Set([...left.keys(), ...right.keys()])) {
      const a = left.get(label) || [], b = right.get(label) || [];
      const pairs = pair(a, b);
      for (const [side, extra] of [['qml', pairs.leftExtra], ['web', pairs.rightExtra]]) {
        for (const entry of extra) {
          const row = { kind, side, text: label, rect: entry.rect,
            allowed: allowed(allow, kind, label, side, extra.length) };
          destination.push(row);
          if (kind === 'controls' && row.allowed) ignoredControls.add(entry);
          if (!row.allowed) failures.push(`${kind}: ${side} only: ${label}`);
        }
      }
      for (const [qa, wb] of pairs.matched) {
        const deltas = ['x', 'y', 'width', 'height'].map(axis => Math.abs(qa.rect[axis] - wb.rect[axis]));
        const tolerances = ['x', 'y', 'width', 'height'].map(axis =>
          Math.max(4, .02 * (axis === 'x' || axis === 'width' ? qml.width : qml.height)));
        const delta = Math.max(...deltas);
        const tolerance = tolerances[deltas.indexOf(delta)];
        if (deltas.some((value, i) => value > tolerances[i])) {
          const row = { kind, text: label, qml: qa.rect, web: wb.rect,
            delta, tolerance,
            allowed: (allow.layout || []).some(x => x.kind === kind && x.text === label && delta <= x.maxDelta
              && normal(x.reason) && normal(x.contract)) };
          layout.push(row);
          if (!row.allowed) failures.push(`layout ${kind}: ${label}: ${delta.toFixed(1)} px`);
        }
      }
    }
  }
  // A label multiset can match while the actual keyboard sequence differs.
  const qOrder = qml.controls.filter(x => !ignoredControls.has(x)).map(x => normal(x.label));
  const wOrder = web.controls.filter(x => !ignoredControls.has(x)).map(x => normal(x.label));
  if (qOrder.join('\0') !== wOrder.join('\0')) {
    controlDiff.push({ kind: 'order', qml: qOrder, web: wOrder, allowed: false });
    failures.push('controls: order differs');
  }
  layout.sort((a, b) => b.delta - a.delta);
  return { pass: failures.length === 0, failures, wordDiff, controlDiff, layout };
}
