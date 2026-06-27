// ReaderEngine.js — pure double-page reader engine, ported VERBATIM from Tankoban Electron's
// src/renderer/src/lib/readerEngine.js (itself from Tankoban-Max render_two_page + state_machine).
// No DOM, no React — pure layout math, so it drops straight into QML as a .pragma library.
// Read-order roles: anchorIndex = read-first (lower index), partnerIndex = read-second.
// Physical left/right mapping lives in computeSpreadLayout via `rtl`.
//
// ctx = { n, isSpreadAt:(i)=>bool, couplingNudge?:0|1 }
.pragma library

var SPREAD_RATIO = 1.08

// A page is a spread if landscape, or its aspect meets the spread ratio.
function isSpread(w, h) {
    if (!w || !h) return false
    return w > h || w / h >= SPREAD_RATIO
}

function clampIndex(i, n) {
    var v = Math.round(Number(i) || 0)
    if (v < 0) return 0
    if (v > n - 1) return Math.max(0, n - 1)
    return v
}

// Count stitched spreads in [1, idx) — the cover (0) is excluded so beginning-of-volume
// pairing stays identical. Each prior spread consumes a parity "slot".
function extraSlotsBefore(idx, ctx) {
    var n = ctx.n || 0
    if (!n) return 0
    var stop = Math.min(Math.max(Math.round(Number(idx) || 0), 0), n)
    var extra = 0
    for (var j = 1; j < stop; j++) if (ctx.isSpreadAt(j)) extra++
    return extra
}

function effectiveIndex(idx, ctx) {
    var n = ctx.n || 0
    if (!n) return 0
    var i = clampIndex(idx, n)
    if (i <= 0) return i // cover stays special
    return i + extraSlotsBefore(i, ctx)
}

// Snap any page to its pair-start (the odd-effective index that begins its spread).
function snapTwoPageIndex(i, ctx) {
    var n = ctx.n || 0
    if (!n) return 0
    var idx = clampIndex(i, n)
    if (idx === 0) return 0
    if (ctx.isSpreadAt(idx)) return idx
    var nudge = ctx.couplingNudge ? 1 : 0
    var eff = effectiveIndex(idx, ctx) + nudge
    if (eff % 2 === 0) {
        var odd = idx - 1
        if (odd <= 0) return idx // never snap onto the cover
        if (ctx.isSpreadAt(odd)) return idx
        return odd
    }
    return idx
}

// Resolve the spread for an anchor page.
//   kind: 'cover' | 'spread' | 'single' | 'pair'
//   anchorIndex = read-first/lower page; partnerIndex = read-second/higher (or null)
function getTwoPagePair(anchor, ctx) {
    var n = ctx.n || 0
    if (!n) return { kind: 'single', anchorIndex: 0, partnerIndex: null }
    var s = snapTwoPageIndex(anchor, ctx)
    if (s === 0) {
        return ctx.isSpreadAt(0)
            ? { kind: 'spread', anchorIndex: 0, partnerIndex: null }
            : { kind: 'cover', anchorIndex: 0, partnerIndex: null }
    }
    if (ctx.isSpreadAt(s)) return { kind: 'spread', anchorIndex: s, partnerIndex: null }
    var nudge = ctx.couplingNudge ? 1 : 0
    if ((effectiveIndex(s, ctx) + nudge) % 2 === 1) {
        var partner = s + 1
        if (partner >= n || ctx.isSpreadAt(partner)) {
            return { kind: 'single', anchorIndex: s, partnerIndex: null }
        }
        return { kind: 'pair', anchorIndex: s, partnerIndex: partner }
    }
    return { kind: 'single', anchorIndex: s, partnerIndex: null }
}

// Step to the next/prev ANCHOR page, or null at the chapter boundary (caller crosses the
// chapter). cover→1, current-page-is-spread→±1, else→±2, then snap.
function stepNext(anchor, ctx) {
    var n = ctx.n || 0
    if (!n) return null
    var idx = clampIndex(anchor, n)
    var target
    if (idx === 0) target = 1
    else if (ctx.isSpreadAt(idx)) target = idx + 1
    else target = idx + 2
    if (!isFinite(target) || target >= n) return null
    return snapTwoPageIndex(target, ctx)
}

function stepPrev(anchor, ctx) {
    var n = ctx.n || 0
    if (!n) return null
    var idx = clampIndex(anchor, n)
    if (idx === 1) return 0 // cover special 1→0
    if (idx <= 0) return null
    var target = ctx.isSpreadAt(idx) ? idx - 1 : idx - 2
    if (!isFinite(target) || target < 0) target = 0
    return snapTwoPageIndex(target, ctx)
}

// Pure pixel layout for a spread, ported from drawTwoPageFrame. Returns rendered px size +
// physical half for each page so the RTL↔LTR mirror lives here, not in the component.
//   args: { kind, anchorDims:{w,h}, partnerDims:{w,h}|null, containerW, containerH, gutter, fitWidth, rtl }
//   → { pages: [{ role:'anchor'|'partner', side:'left'|'right'|'full', w, h }] }
// Caller places each page in its half flush to the spine (left half → align right edge to the
// spine; right half → align left edge), gutter between.
function computeSpreadLayout(args) {
    var kind = args.kind, anchorDims = args.anchorDims, partnerDims = args.partnerDims
    var containerW = args.containerW, containerH = args.containerH
    var gutter = args.gutter, fitWidth = args.fitWidth, rtl = args.rtl

    var leftW = Math.floor((containerW - gutter) / 2)
    var rightW = containerW - gutter - leftW
    var startSide = rtl ? 'right' : 'left' // reading-start half (anchor)
    var otherSide = rtl ? 'left' : 'right'
    var halfPx = function (side) { return side === 'left' ? leftW : rightW }
    var scaleFor = function (dims, halfW) {
        var byW = halfW / Math.max(1e-6, dims.w)
        var byH = containerH / Math.max(1e-6, dims.h)
        return fitWidth ? byW : Math.min(byW, byH)
    }
    var sized = function (dims, scale) { return { w: Math.round(dims.w * scale), h: Math.round(dims.h * scale) } }

    // Any lone page — a wide stitched spread, the cover, or a trailing-unpaired single — renders
    // as ONE full-width page (fitWidth fills the container width like single-page mode).
    if (kind === 'spread' || kind === 'cover' || kind === 'single') {
        var byW = containerW / Math.max(1e-6, anchorDims.w)
        var byH = containerH / Math.max(1e-6, anchorDims.h)
        var scale = fitWidth ? byW : Math.min(byW, byH)
        var one = sized(anchorDims, scale)
        return { pages: [{ role: 'anchor', side: 'full', w: one.w, h: one.h }] }
    }
    // pair: shared scale = min over both halves so the pages meet flush at the spine
    var sc = Math.min(scaleFor(anchorDims, halfPx(startSide)), scaleFor(partnerDims, halfPx(otherSide)))
    var a = sized(anchorDims, sc), p = sized(partnerDims, sc)
    return {
        pages: [
            { role: 'anchor', side: startSide, w: a.w, h: a.h },
            { role: 'partner', side: otherSide, w: p.w, h: p.h }
        ]
    }
}
