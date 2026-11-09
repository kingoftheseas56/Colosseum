.pragma library

function pageUrl(version, platform, universeRequest, desktopUniverse, homeFeedbackKind) {
    if (homeFeedbackKind === "support") return "https://getcolosseum.world/support/app/"
    const report = homeFeedbackKind === "bug" || homeFeedbackKind === "feature"
    return "https://getcolosseum.world/feedback/" + (report ? "report" : desktopUniverse ? "universe" : "")
        + "?version=" + encodeURIComponent(version || "")
        + "&platform=" + encodeURIComponent(platform || "")
        + (report ? "&kind=" + homeFeedbackKind : universeRequest ? "&mode=universe" : "")
}

function isDocumentUrl(value) {
    return /^https:\/\/getcolosseum\.world(?::443)?\/feedback(?:\/|\/index\.html|\/(?:universe|report)(?:\.html)?)?(?:[?#].*)?$/.test(String(value))
}

function isSupportDocumentUrl(value) {
    return /^https:\/\/getcolosseum\.world(?::443)?\/support\/app\/?(?:[?#][^\s\\]*)?$/.test(String(value))
}

function isSupportExternalUrl(value) {
    return /^https:\/\/(?:getcolosseum\.world|(?:www\.)?paypal\.com|paypal\.me)(?::443)?(?:[/?#][^\s\\]*)?$/i.test(String(value))
}
