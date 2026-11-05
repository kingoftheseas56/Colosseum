.pragma library

function pageUrl(version, platform, universeRequest, desktopUniverse, homeFeedbackKind) {
    const report = homeFeedbackKind === "bug" || homeFeedbackKind === "feature"
    return "https://getcolosseum.world/feedback/" + (report ? "report" : desktopUniverse ? "universe" : "")
        + "?version=" + encodeURIComponent(version || "")
        + "&platform=" + encodeURIComponent(platform || "")
        + (report ? "&kind=" + homeFeedbackKind : universeRequest ? "&mode=universe" : "")
}

function isDocumentUrl(value) {
    return /^https:\/\/getcolosseum\.world(?::443)?\/feedback(?:\/|\/index\.html|\/(?:universe|report)(?:\.html)?)?(?:[?#].*)?$/.test(String(value))
}
