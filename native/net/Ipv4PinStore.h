#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

class Ipv4PinStore : public QObject
{
public:
    using LookupDone = std::function<void(QString)>;
    using Lookup = std::function<void(const QString&, LookupDone)>;
    using PinChangedCallback = std::function<void(const QString&, const QString&)>;
    using RefreshFinishedCallback = std::function<void()>;

    explicit Ipv4PinStore(QString cachePath = QString(),
                          Lookup lookup = Lookup(),
                          QObject* parent = nullptr);

    QString pinForHost(const QString& host) const;
    QHash<QString, QString> snapshot() const { return m_pins; }
    QString cachePath() const { return m_cachePath; }

    void setPinChangedCallback(PinChangedCallback callback)
    {
        m_pinChanged = std::move(callback);
    }
    void setRefreshFinishedCallback(RefreshFinishedCallback callback)
    {
        m_refreshFinished = std::move(callback);
    }
    void refresh(const QStringList& hosts);

private:
    void load();
    bool save() const;
    void resolveAttempt(const QString& host, int attempt);
    void finishHost(const QString& host);
    static QString productionCachePath();
    static QString normalizedHost(const QString& host);

    QString m_cachePath;
    Lookup m_lookup;
    PinChangedCallback m_pinChanged;
    RefreshFinishedCallback m_refreshFinished;
    QHash<QString, QString> m_pins;
    QSet<QString> m_inFlight;
};
