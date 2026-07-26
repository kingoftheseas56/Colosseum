// PlayerEngineMpv - the mpv (player 1) branch of PlayerEngine.
//
// Deliberately thin. PlayerPage set nothing on its MpvItem beyond `id`, `anchors.fill` and `z`
// (PlayerPage.qml:2821-2823), and those belong to the facade's own instantiation site, so there
// were no construction properties to carry across. This file exists to keep `Colosseum.Player`
// and the MpvItem type behind the Loader, so PlayerEngine.qml stays engine-agnostic.
//
// Anything added here must also be answered by PlayerEngineP2.qml - PlayerEngine forwards the
// same surface to both branches.
import QtQuick
import Colosseum.Player

MpvItem {
    anchors.fill: parent
}
