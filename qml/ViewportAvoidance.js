.pragma library

function logicalKeyboardRect(rect, scale) {
    const k = rect || ({x: 0, y: 0, width: 0, height: 0})
    const factor = Math.max(1, Number(scale) || 1)
    return {x: k.x / factor, y: k.y / factor,
            width: k.width / factor, height: k.height / factor}
}

function insetRect(width, height, insets, margin) {
    const s = insets || ({})
    const left = Math.max(margin, Number(s.left) || 0)
    const top = Math.max(margin, Number(s.top) || 0)
    const right = Math.max(margin, Number(s.right) || 0)
    const bottom = Math.max(margin, Number(s.bottom) || 0)
    return {x: left, y: top, width: Math.max(0, width - left - right),
            height: Math.max(0, height - top - bottom)}
}

function intersects(a, b) {
    return a.width > 0 && a.height > 0 && b.width > 0 && b.height > 0
        && a.x < b.x + b.width && a.x + a.width > b.x
        && a.y < b.y + b.height && a.y + a.height > b.y
}

function available(width, height, insets, keyboardVisible, keyboard, margin) {
    const gap = margin === undefined ? 12 : margin
    const safe = insetRect(width, height, insets, gap)
    if (!keyboardVisible) return safe
    const k = keyboard || ({x: 0, y: 0, width: 0, height: 0})
    // Android floating IMEs can report an empty rectangle. Keep a conservative,
    // scrollable text-entry area instead of treating the entire display as clear.
    if (k.width <= 0 || k.height <= 0) {
        if (width > height)
            return {x: safe.x, y: safe.y, width: Math.min(safe.width, Math.max(240, safe.width * 0.47)), height: safe.height}
        return {x: safe.x, y: safe.y, width: safe.width, height: Math.min(safe.height, height * 0.45)}
    }
    if (!intersects(safe, k)) return safe
    const regions = [
        {x: safe.x, y: safe.y, width: safe.width, height: Math.max(0, Math.min(safe.y + safe.height, k.y - gap) - safe.y)},
        {x: safe.x, y: safe.y, width: Math.max(0, Math.min(safe.x + safe.width, k.x - gap) - safe.x), height: safe.height},
        {x: Math.max(safe.x, k.x + k.width + gap), y: safe.y, width: Math.max(0, safe.x + safe.width - Math.max(safe.x, k.x + k.width + gap)), height: safe.height},
        {x: safe.x, y: Math.max(safe.y, k.y + k.height + gap), width: safe.width, height: Math.max(0, safe.y + safe.height - Math.max(safe.y, k.y + k.height + gap))}
    ]
    let best = regions[0]
    let score = -1
    for (let i = 0; i < regions.length; ++i) {
        const r = regions[i]
        const usable = Math.min(r.width, 520) * Math.min(r.height, 520)
        const fits = r.width >= Math.min(240, safe.width) && r.height >= Math.min(112, safe.height)
        const rank = usable + (fits ? width * height : 0)
        if (rank > score) { best = r; score = rank }
    }
    return best
}
