.pragma library

function pageUrl(version, platform, universeRequest) {
    return "https://getcolosseum.world/feedback/?version=" + encodeURIComponent(version || "")
        + "&platform=" + encodeURIComponent(platform || "")
        + (universeRequest ? "&mode=universe" : "")
}

function isDocumentUrl(value) {
    return /^https:\/\/getcolosseum\.world(?::443)?\/feedback(?:\/|\/index\.html)?(?:[?#].*)?$/.test(String(value))
}
