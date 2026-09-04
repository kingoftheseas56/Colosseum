#ifndef COLOSSEUM_ANDROIDSEEKTHUMBNAILER_H
#define COLOSSEUM_ANDROIDSEEKTHUMBNAILER_H

#include <QtCore/qglobal.h>

#ifdef Q_OS_ANDROID

#include <QObject>
#include <QUrl>

class AndroidSeekThumbnailer final : public QObject
{
    Q_OBJECT
public:
    explicit AndroidSeekThumbnailer(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void request(const QUrl &source, double timeSec)
    {
        Q_UNUSED(source)
        Q_UNUSED(timeSec)
    }
    Q_INVOKABLE void reset() {}

Q_SIGNALS:
    void thumbReady(double bucketSec, const QString &imageUrl);
};

#endif // Q_OS_ANDROID
#endif // COLOSSEUM_ANDROIDSEEKTHUMBNAILER_H
