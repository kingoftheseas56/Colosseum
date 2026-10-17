// Feria-owned request state. Clock and observations are injected; no storage or
// browser APIs here. A deadline stops seeks, never authorizes unsafe capture.
function idle(token, key) {
    return {token:token, key:key || "", target:0, phase:"idle", blocked:false,
        generation:"", tries:0, confirmations:0, deadline:0, notified:false, anchor:null}
}
function begin(token, key, target, now) {
    var s = idle(token, key)
    s.target = Math.max(0, Number(target) || 0)
    if (s.target > 0) { s.phase = "waiting"; s.blocked = true; s.deadline = now + 15000 }
    return s
}
function invalidate(s, token) {
    // No old target or cancellation is inherited by a new document/player.
    var next = idle(token, s.key)
    if (s.blocked) { next.phase = "changed"; next.blocked = true }
    return next
}
function cancel(s, token, freshStart) {
    if (token !== s.token) return s
    var next = idle(token + 1, s.key)
    if (!freshStart) { next.phase = "cancelled"; next.blocked = true }
    return next
}
function tick(s, sample, key, now) {
    var decision = {allow:false, seek:null, unavailable:false}
    if (!s.blocked) { decision.allow = !!sample; return decision }
    if (sample && key && key !== s.key) {
        // A coherently different logical item owns its own state. It cannot
        // receive the old seek, and its observation cannot overwrite the old ID.
        s.key = key; s.target = 0; s.phase = "idle"; s.blocked = false
        s.generation = String(sample.mediaGeneration || ""); decision.allow = true
        return decision
    }
    if (sample && sample.mediaGeneration) {
        if (s.generation && s.generation !== String(sample.mediaGeneration)) {
            s.target = 0; s.phase = "changed"; s.anchor = null; s.confirmations = 0
        }
        s.generation = String(sample.mediaGeneration)
    }
    if (s.deadline && now >= s.deadline && s.phase !== "settled") s.phase = "failed"
    if ((s.phase === "failed" || s.phase === "changed" || s.phase === "cancelled") && !s.notified) {
        s.notified = true; decision.unavailable = true
    }
    if (!sample || !s.target || sample.ad || sample.readyState < 3
        || !Number.isFinite(sample.duration) || sample.duration < 30
        || !Number.isFinite(sample.position) || sample.position < 0 || sample.position > sample.duration)
        return decision
    var effective = Math.min(s.target, Math.max(0, sample.duration - 1))
    var rate = Number(sample.rate)
    if (!Number.isFinite(rate) || rate <= 0 || rate > 16) return decision
    var anchor = s.anchor
    var dt = anchor ? Math.max(0, (now - anchor.at) / 1000) : 0
    var low = anchor ? anchor.position - 2 : effective - 2
    var high = anchor ? anchor.position + dt * Math.max(anchor.rate, rate) + 2 : effective + 2
    var close = sample.position >= low && sample.position <= high
    if (close) {
        // Only later observations count. Seek assignment/acknowledgement never settles.
        if (!anchor || now > anchor.at) ++s.confirmations
        s.anchor = {position:sample.position, at:now, rate:rate}
        if (s.confirmations >= 2) {
            s.phase = "settled"; s.blocked = false; decision.allow = true
        }
        return decision
    }
    s.confirmations = 0; s.anchor = null
    if (sample.paused) return decision
    if (s.phase === "failed" || s.tries >= 4) {
        s.phase = "failed"
        if (!s.notified) { s.notified = true; decision.unavailable = true }
        return decision
    }
    ++s.tries; s.phase = "confirming"
    s.anchor = {position:effective, at:now, rate:rate}
    decision.seek = effective
    return decision
}
