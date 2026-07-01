function escapeJsSingleQuoted(value) {
    return String(value || "")
        .replace(/\\/g, "\\\\")
        .replace(/'/g, "\\'")
}

function buildOpenScript(path, book) {
    var meta = book || ({})
    var bookId = meta.id !== undefined && meta.id !== null ? String(meta.id) : ""
    var title = meta.title !== undefined && meta.title !== null ? String(meta.title) : ""
    return "window.__ebookOpenBook('"
        + escapeJsSingleQuoted(path)
        + "','"
        + escapeJsSingleQuoted(bookId)
        + "','"
        + escapeJsSingleQuoted(title)
        + "')"
}

if (typeof module !== "undefined" && module.exports) {
    module.exports = {
        escapeJsSingleQuoted: escapeJsSingleQuoted,
        buildOpenScript: buildOpenScript
    }
}
