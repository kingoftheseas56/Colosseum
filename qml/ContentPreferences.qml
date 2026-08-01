// ContentPreferences — the ONE global preference store the whole shell reads.
// Task 2 (Tankoban Discover): a single persisted setting, `showExplicit`, backed by
// QtCore Settings under the [content] category. Production leaves `settingsLocation`
// unset so Qt uses the application QSettings store; the offscreen harness injects a
// temporary INI url through the alias so a test never touches the real store.
// (Threading this preference into Theatre/Tankoban/Biblio is Task 9 — not here.)
import QtQuick
import QtCore

QtObject {
    id: root
    property alias settingsLocation: store.location
    property alias showExplicit: store.showExplicit
    signal changed()
    property Settings settingsStore: Settings {
        id: store
        category: "content"
        property bool showExplicit: false
        onShowExplicitChanged: root.changed()
    }
}
