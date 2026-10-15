.pragma library
// Stage 2 warming — pure pick logic (headless-testable). Given the mode names already
// in the keep-alive stack (openModes) and the ordered target worlds, return the next
// world to pre-build (first target not yet present), or "" when all are warmed.
function nextWarmMode(openModeNames, targets) {
    for (var i = 0; i < (targets || []).length; i++) {
        var t = targets[i];
        if ((openModeNames || []).indexOf(t) === -1) return t;
    }
    return "";
}
