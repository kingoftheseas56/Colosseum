// HostedPlayerBridge.h
//
// The least-privilege QWebChannel gate between the VidKing wrapper page and QML.
// This object is the ONLY thing the hosted (cross-origin) VidKing iframe can reach
// through the wrapper, so it is deliberately tiny and paranoid:
//
//   - it exposes exactly ONE invokable — postPlayerEvent(json);
//   - it emits exactly ONE signal — playerEvent(map);
//   - it has NO file, network, shell, extension, progress-store, or navigation surface.
//
// postPlayerEvent parses + bounds-validates the JSON IN C++ and copies only an
// allowlist of fields; a malformed, out-of-bounds, or unknown-event payload is
// silently dropped, never forwarded. (Theatre VidKing plan, 2026-08-02, Task 5.)
#pragma once

#include <QObject>
#include <QVariantMap>

class HostedPlayerBridge : public QObject
{
    Q_OBJECT
public:
    explicit HostedPlayerBridge(QObject *parent = nullptr) : QObject(parent) {}

    // Called from the wrapper's JS over the WebChannel with a small JSON string.
    // Emits playerEvent only for a well-formed, in-bounds VidKing PLAYER_EVENT.
    Q_INVOKABLE void postPlayerEvent(const QString &json);

signals:
    // The sanitized, allowlisted event map. QML connects to this and nothing else.
    void playerEvent(const QVariantMap &event);
};
