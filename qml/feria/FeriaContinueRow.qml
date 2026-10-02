import QtQuick
import ".." as Colosseum

Colosseum.ContinueRow {
    id: root
    required property var controller
    automationId: "feriaContinueRail"
    title: "Continue"
    showSeeAll: false
    // Keep the provider session intact for resume; adapt only its presentation.
    items: controller.continueItems.filter(function(row) {
        return controller.lens === "all" || (controller.lens === "read" ? row.kind === "book"
            : controller.lens === "listen" ? row.kind === "audio" : row.kind === "video")
    }).map(function(row) {
        var entry = Object.assign({}, row)
        entry.progress = row.duration > 0 ? row.position / row.duration : 0
        entry.source = controller.providerName(row.pk)
        entry.sub = entry.source + " · " + (row.kind === "book"
            ? (row.duration > 0 ? Math.round(entry.progress * 100) + "% read" : "Saved reading place")
            : controller.durationText(row.position / 60) + " / " + controller.durationText(row.duration / 60))
        return entry
    })
    forgetHandler: function(entry) { controller.accountStore.dismissContinue(entry.id) }
    onResumeRequested: (entry) => controller.resumeSession(entry)
    onDetailRequested: (entry) => controller.resumeSession(entry)
}
