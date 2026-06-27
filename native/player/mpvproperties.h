// mpv property-name constants, lifted from KDE's mpvqt video-player example.
// Plain C++ helper (no Q_OBJECT / QML macros) — Colosseum only touches these from
// MpvItem.cpp, never from QML, so it needs no moc and no qml registration.
#ifndef COLOSSEUM_MPVPROPERTIES_H
#define COLOSSEUM_MPVPROPERTIES_H

#include <QString>

class MpvProperties
{
public:
    static MpvProperties *self()
    {
        static MpvProperties p;
        return &p;
    }

    const QString MediaTitle{QStringLiteral("media-title")};
    const QString Position{QStringLiteral("time-pos")};
    const QString Duration{QStringLiteral("duration")};
    const QString Pause{QStringLiteral("pause")};
    const QString Volume{QStringLiteral("volume")};
    const QString Mute{QStringLiteral("mute")};

private:
    MpvProperties() = default;
    Q_DISABLE_COPY_MOVE(MpvProperties)
};

#endif // COLOSSEUM_MPVPROPERTIES_H
