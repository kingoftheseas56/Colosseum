.pragma library
var MIN_UPVOTES = 10
var WORDS_PER_MINUTE = 210
var READING_LEAD_MS = 500
var MIN_AUTO_WINDOW_MS = 5000
var MAX_AUTO_WINDOW_MS = 7500

function wordCount(text) {
    if (typeof text !== "string") return 0
    var trimmed = text.trim()
    if (trimmed.length === 0) return 0
    return trimmed.split(/\s+/).length
}

function isCredible(note) {
    if (!note || typeof note !== "object") return false
    if (note.verified === true) return true
    return typeof note.votes === "number" && isFinite(note.votes) && note.votes >= MIN_UPVOTES
}

function pick(notes, availableMs) {
    if (typeof availableMs !== "number" || !isFinite(availableMs) || availableMs <= 0)
        return null
    if (!notes || !notes.length) return null

    var chosen = null
    var chosenWordCount = 0
    for (var i = 0; i < notes.length; ++i) {
        var note = notes[i]
        if (!isCredible(note)) continue

        var candidateWordCount = wordCount(note.text)
        if (candidateWordCount === 0) continue
        if (chosen === null || candidateWordCount < chosenWordCount) {
            chosen = note
            chosenWordCount = candidateWordCount
        }
    }

    if (chosen === null) return null
    var readingTimeMs = chosenWordCount * 60000 / WORDS_PER_MINUTE + READING_LEAD_MS
    return readingTimeMs <= availableMs ? chosen : null
}

function isUrlToken(token) {
    var value = token
    while (value.length && /^[([{<"'“‘]/.test(value.charAt(0))) value = value.substring(1)
    while (value.length && /[)\]}>.,!?;:"'”’…]$/.test(value.charAt(value.length - 1)))
        value = value.substring(0, value.length - 1)
    return /^(?:(?:https?|ftp):\/\/|www\.)\S+$/i.test(value)
            || /^(?:[a-z0-9-]+\.)+[a-z]{2,}(?::\d+)?(?:[/?#]\S*)?$/i.test(value)
}

function isUrlOnly(text) {
    var trimmed = text.trim()
    if (trimmed.length === 0) return false
    var tokens = trimmed.split(/\s+/)
    for (var i = 0; i < tokens.length; ++i) {
        if (!isUrlToken(tokens[i])) return false
    }
    return true
}

function isAbbreviationToken(token) {
    var compact = token.replace(/^["'“‘([{]+|["'”’)}\]]+$/g, "")
    return /^(?:dr|mr|mrs|ms|prof|sr|jr|st|vs|etc)\.?$/i.test(compact)
            || /^(?:[a-z]\.){2,}$/i.test(compact)
}

function isAbbreviationOnly(text) {
    var tokens = text.trim().split(/\s+/)
    if (tokens.length === 0 || tokens[0].length === 0) return false
    for (var i = 0; i < tokens.length; ++i) {
        if (!isAbbreviationToken(tokens[i])) return false
    }
    return true
}

function isReadableBoundary(text) {
    return text.length > 0 && !isUrlOnly(text) && !isAbbreviationOnly(text)
}

function firstParagraph(text) {
    var separator = /\r?\n[ \t]*\r?\n/.exec(text)
    return separator ? text.substring(0, separator.index).trim() : ""
}

function firstSentence(text) {
    for (var i = 0; i < text.length; ++i) {
        if (text.charAt(i) !== "." && text.charAt(i) !== "?" && text.charAt(i) !== "!") continue
        var end = i + 1
        while (end < text.length && /[\"'”’)}\]]/.test(text.charAt(end))) ++end
        if (end < text.length && !/\s/.test(text.charAt(end))) continue
        var candidate = text.substring(0, end).trim()
        if (!isReadableBoundary(candidate)) continue
        return candidate
    }
    return ""
}

function prefixAtWord(text, count) {
    var words = 0
    var inWord = false
    for (var i = 0; i < text.length; ++i) {
        if (/\s/.test(text.charAt(i))) {
            if (inWord) {
                ++words
                inWord = false
                if (words >= count) return text.substring(0, i).trim()
            }
        } else {
            inWord = true
        }
    }
    return text.trim()
}

function clausePrefix(text, wordCap) {
    var best = ""
    var bestWords = 0
    for (var i = 0; i < text.length; ++i) {
        var character = text.charAt(i)
        if (character !== "," && character !== ";" && character !== ":"
                && character !== "-" && character !== "–" && character !== "—") continue
        var end = i + 1
        while (end < text.length && /[\"'”’)}\]]/.test(text.charAt(end))) ++end
        if (end < text.length && !/\s/.test(text.charAt(end))) continue
        var candidate = text.substring(0, end).trim()
        var candidateWords = wordCount(candidate)
        if (candidateWords < 4 || candidateWords > wordCap || !isReadableBoundary(candidate)) continue
        if (candidateWords > bestWords) {
            best = candidate
            bestWords = candidateWords
        }
    }
    return best
}

function autoPrefix(text, wordCap) {
    var paragraph = firstParagraph(text)
    if (wordCount(paragraph) > 0 && wordCount(paragraph) <= wordCap && isReadableBoundary(paragraph))
        return paragraph

    var sentence = firstSentence(text)
    if (sentence.length > 0 && wordCount(sentence) <= wordCap) return sentence

    var clause = clausePrefix(text, wordCap)
    if (clause.length > 0) return clause
    return prefixAtWord(text, wordCap)
}

function pickAuto(notes, availableMs) {
    if (typeof availableMs !== "number" || !isFinite(availableMs) || availableMs <= 0)
        return null
    if (!Array.isArray(notes) || notes.length === 0) return null

    var chosen = null
    var chosenWordCount = 0
    var chosenText = ""
    for (var i = 0; i < notes.length; ++i) {
        var note = notes[i]
        if (!isCredible(note) || typeof note.text !== "string") continue
        var sourceText = note.text.trim()
        if (sourceText.length === 0 || isUrlOnly(sourceText) || isAbbreviationOnly(sourceText)) continue
        var candidateWordCount = wordCount(sourceText)
        if (candidateWordCount === 0) continue
        if (chosen === null || candidateWordCount < chosenWordCount) {
            chosen = note
            chosenWordCount = candidateWordCount
            chosenText = sourceText
        }
    }

    if (chosen === null) return null
    var boundedWindowMs = Math.max(MIN_AUTO_WINDOW_MS, Math.min(MAX_AUTO_WINDOW_MS, availableMs))
    var wordCap = Math.max(15, Math.min(24,
        Math.floor((boundedWindowMs - READING_LEAD_MS) * WORDS_PER_MINUTE / 60000)))
    var excerpt = autoPrefix(chosenText, wordCap)
    var isExcerpt = excerpt.length < chosenText.length
    var resultText = isExcerpt ? excerpt + "\u2026" : excerpt
    var autoReadMs = Math.ceil(wordCount(resultText) * 60000 / WORDS_PER_MINUTE + READING_LEAD_MS)
    autoReadMs = Math.max(MIN_AUTO_WINDOW_MS, Math.min(MAX_AUTO_WINDOW_MS, autoReadMs))

    var result = {}
    for (var property in chosen) {
        if (Object.prototype.hasOwnProperty.call(chosen, property)) result[property] = chosen[property]
    }
    result.text = resultText
    result.isExcerpt = isExcerpt
    result.autoReadMs = autoReadMs
    return result
}
