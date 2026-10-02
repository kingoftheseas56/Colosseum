#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QVariantMap>
#include <QVariantList>

// Local, profile-owned Feria activity. Provider credentials stay in the browser.
class FeriaAccountStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY changed)
    Q_PROPERTY(QVariantList continueItems READ continueItems NOTIFY changed)
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit FeriaAccountStore(QObject *parent = nullptr);
    void setStoragePath(const QString &path);
    QVariantList sessions() const;
    QVariantList continueItems() const;
    bool recording() const;
    QString error() const { return m_error; }
    Q_INVOKABLE void beginVisit(const QVariantMap &context);
    Q_INVOKABLE void endVisit();
    Q_INVOKABLE bool observe(const QVariantMap &sample);
    Q_INVOKABLE bool saveReadingPlace(const QString &url, const QString &title);
    Q_INVOKABLE bool setRecording(bool enabled);
    Q_INVOKABLE bool clearHistory();
    Q_INVOKABLE bool dismissContinue(const QString &id);
    static QString safeUrl(const QString &url);
signals:
    void changed();
    void profileChanged();
private:
    bool commit(const QVariantMap &state);
    bool record(const QVariantMap &sample, bool reading);
    QVariantMap m_state, m_context;
    QString m_path, m_error, m_sessionId, m_lastItem;
    double m_lastPosition = 0;
    bool m_lastPlaying = false;
    QElapsedTimer m_clock;
};
