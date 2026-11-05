.pragma library

function pageUrl(version, platform, universeRequest, desktopUniverse) {
    return "https://getcolosseum.world/feedback/" + (desktopUniverse ? "universe" : "")
        + "?version=" + encodeURIComponent(version || "")
        + "&platform=" + encodeURIComponent(platform || "")
        + (universeRequest ? "&mode=universe" : "")
}

function isDocumentUrl(value) {
    return /^https:\/\/getcolosseum\.world(?::443)?\/feedback(?:\/|\/index\.html|\/universe(?:\.html)?)?(?:[?#].*)?$/.test(String(value))
}
