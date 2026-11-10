#pragma once

#include <QJniObject>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QWindow>

class AndroidFeedbackBrowser final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QWindow* foreignWindow READ foreignWindow NOTIFY foreignWindowChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
public:
    explicit AndroidFeedbackBrowser(QObject* parent = nullptr);
    ~AndroidFeedbackBrowser() override;
    QWindow* foreignWindow() const { return m_window; }
    bool loading() const { return m_loading; }
    QString errorText() const { return m_error; }
    Q_INVOKABLE void open(const QUrl& url, bool bundledSupport = false);
    Q_INVOKABLE void close();
    void receive(quint64 instance, const QString& event);
signals:
    void foreignWindowChanged();
    void stateChanged();
    void closeRequested();
    void externalLinkRequested(const QUrl& destination);
private:
    void fail();
    QPointer<QWindow> m_window;
    QJniObject m_host;
    quint64 m_instance = 0;
    bool m_loading = false;
    QString m_error;
};
