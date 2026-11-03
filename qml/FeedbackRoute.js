.pragma library

function pageUrl(version, platform) {
    return "https://getcolosseum.world/feedback/?version=" + encodeURIComponent(version || "")
        + "&platform=" + encodeURIComponent(platform || "")
}

function isDocumentUrl(value) {
    return /^https:\/\/getcolosseum\.world(?::443)?\/feedback(?:\/|\/index\.html)?(?:[?#].*)?$/.test(String(value))
}
