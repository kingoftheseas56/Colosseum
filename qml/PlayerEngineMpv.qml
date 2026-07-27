// PlayerEngineMpv - the mpv (player 1) branch of PlayerEngine.
//
// Deliberately thin. PlayerPage set nothing on its MpvItem beyond `id`, `anchors.fill` and `z`
// (PlayerPage.qml:2821-2823), and those belong to the facade's own instantiation site, so there
// were no construction properties to carry across. This file exists to keep `Colosseum.Player` and
// the MpvItem type behind the Loader, so PlayerEngine.qml never imports or names an engine. That is
// not full engine-agnosticism and the claim should not be made: the surface PlayerEngine forwards IS
// mpv's, and its capability flags (supportsCapture / supportsLive) are decided by which branch
// loaded, not asked of the branch.
//
// Anything added here must also be answered by PlayerEngineP2.qml - PlayerEngine forwards the
// same surface to both branches.
import QtQuick
import Colosseum.Player

MpvItem {
    anchors.fill: parent
}
