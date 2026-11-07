// A deliberately small, disclosed proof plugin, not a community-provider
// compatibility claim. The original B&W Night of the Living Dead is public
// domain: https://blogs.loc.gov/copyright/2020/10/copyright-horror-stories/
// Metadata is fetched live; an absent/relicensed/missing file yields no rows.
const identifier = "Night_Of_The_Living_Dead_raw_HD_WS";
const filename = "NightOfTheLivingDeadRawUnrestored.mp4";

module.exports.getStreams = async function (tmdbId, mediaType) {
    if (String(tmdbId) !== "10331" || mediaType !== "movie") return [];

    // This verifies the unchanged JS-side credential boundary without needing
    // an actual key. Authorizing TMDB HTTPS requests remains native-only.
    const placeholder = "__COLOSSEUM_TMDB_KEY__";
    if (TMDB_API_KEY !== placeholder || TMDB_ACCESS_TOKEN !== placeholder ||
        process.env.TMDB_API_KEY !== placeholder ||
        process.env.TMDB_ACCESS_TOKEN !== placeholder ||
        Object.keys(process.env).length !== 2) {
        throw new Error("Unexpected credential boundary");
    }

    const response = await fetch("https://archive.org/metadata/" + identifier);
    if (!response.ok) return [];
    const body = await response.json();
    const metadata = body && body.metadata;
    if (!metadata || metadata.identifier !== identifier ||
        !/^https?:\/\/creativecommons\.org\/publicdomain\/mark\/1\.0\/$/.test(String(metadata.licenseurl || "")) ||
        !/night of the living dead/i.test(String(metadata.title || "")) ||
        !Array.isArray(body.files)) return [];

    const file = body.files.find(function (entry) {
        return entry && entry.name === filename && entry.format === "h.264 MPEG4" &&
               Number(entry.size) > 0 && Number(entry.size) < 2 * 1024 * 1024 * 1024;
    });
    if (!file) return [];
    return [{
        name: "Internet Archive · public-domain original",
        title: "Night of the Living Dead (1968) · original B&W",
        url: "https://archive.org/download/" + identifier + "/" + filename
    }];
};
