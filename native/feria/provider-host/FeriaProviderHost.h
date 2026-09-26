#pragma once

#include <QObject>
#include <QRect>
#include <QString>
#include <QUrl>

#include <memory>

class FeriaProviderHost final : public QObject
{
    Q_OBJECT

public:
    explicit FeriaProviderHost(QObject *parent = nullptr);
    ~FeriaProviderHost() override;

    void initialize(quintptr parentWindowId,
                    const QString &userDataFolder,
                    const QUrl &initialUrl);
    void setPhysicalBounds(const QRect &bounds);
    QRect physicalBounds() const;
    QRect nativeChildBounds() const;
    QString nativeChildClassName() const;
    void setVisible(bool visible);
    bool isVisible() const;
    bool isReady() const;
    void focusWebView();
    void navigate(const QUrl &url);
    void executeScript(const QString &label, const QString &script);
    void pauseMedia(const QString &label);

signals:
    void readyChanged(bool ready);
    void navigationBlocked(const QString &uri);
    void navigationCompleted(const QString &uri, bool success);
    void scriptResult(const QString &label, const QString &jsonResult);
    void observerMessage(const QString &jsonMessage);
    void escapeRequested();
    void popupOpened(const QString &uri);
    void hostLog(const QString &message);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
