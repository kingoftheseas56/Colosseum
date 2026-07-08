#pragma once

// Clipboard — one-verb native helper exposed to QML as `Clipboard`.
// QML has no system-clipboard reach of its own; this wraps the one call it needs
// (spec 2026-07-08: copy-magnet on the sources sheet). Nothing else lives here.

#include <QObject>
#include <QString>
#include <QGuiApplication>
#include <QClipboard>

class ClipboardHelper : public QObject {
    Q_OBJECT
public:
    explicit ClipboardHelper(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void copy(const QString &text) const {
        if (auto *cb = QGuiApplication::clipboard())
            cb->setText(text);
    }
};
