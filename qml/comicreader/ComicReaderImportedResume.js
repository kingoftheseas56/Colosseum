// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.
.pragma library

function _finiteNumber(value, fallback) {
    var n = Number(value)
    return isFinite(n) ? n : fallback
}

function _looksLikeFilesystemPath(value) {
    var s = String(value || "").trim()
    if (!s) return false

    var lower = s.toLowerCase()
    if (lower.indexOf("file:/") === 0 || lower.indexOf("qrc:/") === 0)
        return true
    if (/^[a-zA-Z]:[\\/]/.test(s))
        return true
    if (/^[\\]{2}/.test(s))
        return true
    if (/^\//.test(s))
        return true
    if (/^(\.\.\/|\.\/|\.\.\\|\.\\)/.test(s))
        return true
    return false
}

function resolve(record, expectedSeriesId) {
    if (!record || typeof record !== "object")
        return { valid: false, code: "missing_record" }

    var seriesId = String(record.id || "").trim()
    var expected = String(expectedSeriesId || "").trim()
    if (!seriesId || !expected || seriesId !== expected)
        return { valid: false, code: "series_mismatch" }

    if (String(record.kind || "") !== "tankoban")
        return { valid: false, code: "kind_mismatch" }

    var resume = record.resume
    if (!resume || typeof resume !== "object")
        return { valid: false, code: "missing_resume" }

    var chapterId = String(resume.chapterId || "").trim()
    if (!chapterId)
        return { valid: false, code: "missing_chapter" }
    if (_looksLikeFilesystemPath(chapterId))
        return { valid: false, code: "filesystem_identity" }

    var page = Math.floor(_finiteNumber(resume.page, -1))
    if (page < 0)
        return { valid: false, code: "invalid_page" }

    var pageFraction = _finiteNumber(resume.pageFraction, 0)
    if (pageFraction < 0 || pageFraction > 1)
        return { valid: false, code: "invalid_page_fraction" }

    var scrollFrac = _finiteNumber(resume.scrollFrac, 0)
    if (scrollFrac < 0 || scrollFrac > 1)
        scrollFrac = 0

    return {
        valid: true,
        code: "",
        seriesId: seriesId,
        chapterId: chapterId,
        page: page,
        pageFraction: pageFraction,
        legacyScrollFrac: scrollFrac,
        updatedAt: _finiteNumber(record.updatedAt, 0)
    }
}

function fingerprint(target) {
    if (!target || target.valid !== true)
        return ""
    // Reader repositioning depends only on semantic resume location. A remote
    // metadata/progress update with the same chapter/page anchor must not make
    // an already-open reader jump to the same place again.
    return [
        String(target.seriesId || ""),
        String(target.chapterId || ""),
        String(target.page),
        String(target.pageFraction)
    ].join("|")
}
