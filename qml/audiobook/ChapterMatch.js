.pragma library

var ordinal = "(?:\\d+|(?=[mdclxvi]+\\b)m{0,3}(?:cm|cd|d?c{0,3})(?:xc|xl|l?x{0,3})(?:ix|iv|v?i{0,3}))"
var genericOrdinal = new RegExp("^(?:(?:chapter|ch|track|part|disc|disk|cd|book|section|interlude|file|segment) ?)?"
                                + ordinal + "(?:(?: of)? " + ordinal + ")*$")
var chapterPrefix = new RegExp("^(?:chapter|ch) " + ordinal + " ")
var interludePrefix = new RegExp("^interlude (?:" + ordinal + "(?: \\d+)? )?")

function titleKey(row) {
    var title = typeof row === "string" ? row : row && (row.label || row.title)
    if (typeof title !== "string" || /\.(?:m4b|m4a|mp3|ogg|opus|flac|aac|wav|wma|aax)$/i.test(title.trim()))
        return ""

    // Qt's JS engine does not support Unicode property escapes. Keep letters across
    // scripts while folding ASCII and common Unicode punctuation to word boundaries.
    var key = title.normalize("NFC").toLowerCase()
        .replace(/['\u2018\u2019]/g, "")
        .replace(/[\s\u0021-\u002F\u003A-\u0040\u005B-\u0060\u007B-\u007E\u00A1\u00AB\u00BB\u00BF\u2010-\u2027\u2030-\u205E\u3001-\u3003\u3008-\u301F]+/g, " ")
        .trim()
    if (genericOrdinal.test(key))
        return ""

    key = key.replace(chapterPrefix, "").replace(interludePrefix, "")
    return genericOrdinal.test(key) ? "" : key
}

function keys(rows, audio) {
    return Array.from(rows, function(row) {
        return audio && row && (row.kind === "file" || row.synthetic) ? "" : titleKey(row)
    })
}

function uniquePositions(titles) {
    var positions = new Map()
    for (var index = 0; index < titles.length; index++) {
        var title = titles[index]
        if (title)
            positions.set(title, positions.has(title) ? -1 : index)
    }
    return positions
}

function match(audioChapterModel, ebookTocRows) {
    var unavailable = { aligned: false, audioToEbook: {}, ebookToAudio: {} }
    if (!audioChapterModel || !audioChapterModel.length || !ebookTocRows || !ebookTocRows.length)
        return unavailable

    var audioTitles = keys(audioChapterModel, true)
    var audioPositions = uniquePositions(audioTitles)
    var ebookPositions = uniquePositions(keys(ebookTocRows, false))
    var audioToEbook = {}
    var ebookToAudio = {}
    var matched = 0
    var previousEbookIndex = -1
    for (var audioIndex = 0; audioIndex < audioTitles.length; audioIndex++) {
        var title = audioTitles[audioIndex]
        var ebookIndex = ebookPositions.get(title)
        if (audioPositions.get(title) !== audioIndex || ebookIndex === undefined || ebookIndex < 0)
            continue
        if (ebookIndex <= previousEbookIndex)
            return unavailable
        audioToEbook[audioIndex] = ebookIndex
        ebookToAudio[ebookIndex] = audioIndex
        previousEbookIndex = ebookIndex
        matched++
    }

    // Require two thirds of both complete sequences, including unmatched rows.
    if (matched * 3 < 2 * Math.max(audioChapterModel.length, ebookTocRows.length))
        return unavailable
    if (matched === 1 && audioChapterModel[0].kind !== "seek")
        return unavailable
    return { aligned: true, audioToEbook: audioToEbook, ebookToAudio: ebookToAudio }
}
