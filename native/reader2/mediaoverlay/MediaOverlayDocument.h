#pragma once
#include <QByteArray>
#include <QStringList>
#include <QVector>
#include <optional>
namespace MediaOverlay {
struct Warning {
    QString code;
    QString message;
    QString source;
    qint64 line = 0;
    qint64 position = 0; // One-based par position, or manifest/spine item when applicable.
};
struct Sequence {
    QString id;
    QStringList types;
    QString textRef;
};
struct OverlayEntry {
    QString id;
    QString textPath;
    QString fragmentId;
    QString audioPath;
    qint64 beginMs = 0;
    std::optional<qint64> endMs;
    bool textOnly = true;
    QStringList types;
    QVector<Sequence> sequences;
};
struct DocumentResult {
    bool ok = false;
    QString error;
    qint64 errorLine = 0;
    QVector<OverlayEntry> entries;
    QVector<Warning> warnings;
};
struct MediaOverlayDocument {
    static DocumentResult parse(const QByteArray &xml);
};
}
