.pragma library

function display(chapter, index, chapterPattern) {
    if (!chapter)
        return ""

    const title = String(chapter.label || "").trim()
    // File rows carry the basename, even when it has no extension.
    const filename = chapter.kind === "file"
        || /\.(?:mp3|m4[ab]|aac|flac|ogg|oga|opus|wav|wma|webm|mp4|aiff?)$/i.test(title)
    const numbered = /^(?:(?:track|chapter)\s*[-_#:.)]?\s*)?\d+\.?$/i.test(title)
    return title && !filename && !numbered ? title : chapterPattern.arg(index + 1)
}
