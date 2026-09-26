#pragma once

#include <QWebEngineUrlSchemeHandler>
#include <QString>

class QWebEngineUrlRequestJob;

class WallpaperSchemeHandler final : public QWebEngineUrlSchemeHandler {
    Q_OBJECT
public:
    explicit WallpaperSchemeHandler(QObject *parent = nullptr);
    static void registerScheme(); // before QGuiApplication and WebEngine initialize
    QString publicUrl(const QString &source);
    void requestStarted(QWebEngineUrlRequestJob *job) override;

private:
    QString m_localPath;
    quint64 m_revision = 0;
};
