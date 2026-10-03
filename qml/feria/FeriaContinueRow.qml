import QtQuick
import ".." as Colosseum

Colosseum.ContinueRow {
    id: root
    required property var controller
    automationId: "feriaContinueRail"
    title: "Continue"
    showSeeAll: true
    onSeeAllRequested: controller.openSeeAll(null)
    // Keep the provider session intact for resume; adapt only its presentation.
    items: controller.continueEntries()
    forgetHandler: function(entry) { controller.accountStore.dismissContinue(entry.id) }
    onResumeRequested: (entry) => controller.resumeSession(entry)
    onDetailRequested: (entry) => controller.resumeSession(entry)
}
