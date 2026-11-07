// Controlled scheduler proof, not a community provider or live metadata check.
module.exports.getStreams = async function(tmdbId, mediaType) {
    if (String(tmdbId) !== "10331" || mediaType !== "movie") return [];
    await new Promise(resolve => setTimeout(resolve, 5000));
    return [{name: "Controlled concurrency slot 4", title: "Public-domain Night of the Living Dead", url: "https://archive.org/download/Night_Of_The_Living_Dead_raw_HD_WS/NightOfTheLivingDeadRawUnrestored.mp4"}];
};
