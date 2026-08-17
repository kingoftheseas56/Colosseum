// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

import QtQuick
import ".."

AccountPageFrame {
    id: root
    objectName: "accountPendingSyncSignOut"

    required property var controller
    property Item backdrop: null

    eyebrow: "COLOSSEUM · ACCOUNT"
    headline: "Some changes haven't synced"
    detail: ""
    panelWidth: 500
    panelMinimumHeight: 180

    AccountButton {
        objectName: "accountPendingSyncRetry"
        width: parent.width
        text: "Stay and retry"
        variant: "primary"
        onClicked:
            root.controller.stayAndRetrySignOut()
    }

    Item {
        width: 1
        height: 12
    }

    AccountButton {
        objectName: "accountPendingSyncAnyway"
        width: parent.width
        text: "Sign out anyway"
        onClicked:
            root.controller.signOutAnyway()
    }
}
