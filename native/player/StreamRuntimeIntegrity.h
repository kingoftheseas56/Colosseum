#pragma once

#include <QByteArray>

namespace StreamRuntimeIntegrity {

// The upstream engine verifies bytes but keeps requesting from a supplier even
// after that supplier alone provided a corrupt piece. Keep attribution on the
// actual PieceBuffer, so canceled/duplicate callbacks cannot frame another peer.
inline const QByteArray helpers = R"JS(
    function colosseumRecordPieceSupplier(piece, reservation, wire) {
        if (piece.get(reservation)) return;
        var sources = piece.colosseumSuppliers || (piece.colosseumSuppliers = []);
        if (sources.length < 2 && sources.indexOf(wire) < 0) sources.push(wire);
    }
    function colosseumRejectCorruptSupplier(engine, piece, verification) {
        var sources = piece.colosseumSuppliers;
        // A virtual subpiece is insufficient evidence about the original hash.
        // Mixed-source failures likewise cannot identify an individual culprit.
        if (!verification || verification.success || verification.end - verification.start !== 1
            || !sources || sources.length !== 1) return;
        var wire = sources[0], address = wire.peerAddress;
        if (address) {
            var banned = engine.colosseumCorruptPeers || (engine.colosseumCorruptPeers = Object.create(null));
            // Bound retained endpoint identities for this engine's lifetime.
            if (Object.keys(banned).length < 256) banned[address] = true;
        }
        engine.emit("colosseum-corrupt-peer");
        wire.destroy();
    }
    function colosseumRejectKnownCorruptPeer(engine, wire) {
        if (!engine.colosseumCorruptPeers || !engine.colosseumCorruptPeers[wire.peerAddress]) return false;
        wire.destroy();
        return true;
    }
)JS";

// Patch only the recognized engine shape, atomically. External service installs
// are never edited; this is applied to the owned child script in our cache.
inline bool apply(QByteArray &script)
{
    if (script.contains("function colosseumRecordPieceSupplier(")) return true;
    const QByteArray factory = "module.exports = function(link, opts) {";
    const QByteArray receive = "var ready = !p.set(reservation, block);";
    const QByteArray invalid = "else for (var j = ver.start; j != ver.end; j++) engine.emit(\"invalid-piece\", j),";
    const QByteArray connected = "onwire = function(wire) {";
    const QByteArray prewarm = "sel = engine.select(offsetPiece, endPiece, !1);";
    for (const auto &anchor : {factory, receive, invalid, connected, prewarm})
        if (script.count(anchor) != 1) return false;
    script.replace(factory, helpers + factory);
    script.replace(receive, "colosseumRecordPieceSupplier(p, reservation, wire); " + receive);
    script.replace(invalid, "else for (var j = (colosseumRejectCorruptSupplier(engine, p, ver), ver.start); j != ver.end; j++) engine.emit(\"invalid-piece\", j),");
    script.replace(connected, connected + "\n                if (colosseumRejectKnownCorruptPeer(engine, wire)) return;");
    // Reopened HTTP ranges must not accumulate duplicate whole-file selections.
    script.replace(prewarm, "if (!sel || engine.selection.indexOf(sel) < 0) " + prewarm);
    return true;
}

} // namespace StreamRuntimeIntegrity
