.pragma library

// Interface typography only; document appearance and scene density have separate owners.
var androidInterfaceScale = 1.20

function fontSize(base, androidHost, systemFontScale) {
    if (!androidHost || typeof base !== "number" || !isFinite(base) || base <= 0)
        return base
    var systemScale = typeof systemFontScale === "number" && isFinite(systemFontScale)
        && systemFontScale > 0 ? systemFontScale : 1
    return Math.max(1, Math.round(base * androidInterfaceScale * systemScale))
}
