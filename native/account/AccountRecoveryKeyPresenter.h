#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountOneTimeSecretSink.h"
#include "AccountSensitiveClipboard.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

class AccountRecoveryKeyPresenter final
    : public QObject,
      public AccountOneTimeSecretSink {
    Q_OBJECT

    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString purpose READ purpose NOTIFY purposeChanged)
    // Intentionally sensitive and transient. This property exists only on the
    // dedicated one-time presenter, never on AccountController.
    Q_PROPERTY(QString recoveryKey READ recoveryKey NOTIFY recoveryKeyChanged)
    Q_PROPERTY(QString copyState READ copyState NOTIFY copyStateChanged)

public:
    explicit AccountRecoveryKeyPresenter(
        AccountSensitiveClipboard *clipboard,
        QObject *parent = nullptr);
    ~AccountRecoveryKeyPresenter() override;

    bool active() const;
    QString purpose() const;
    QString recoveryKey() const;
    QString copyState() const;

    bool presentRecoveryKey(
        const QString &recoveryKey,
        AccountRecoveryKeyPurpose purpose) override;

    Q_INVOKABLE bool copyRecoveryKey();
    Q_INVOKABLE void dismiss();

    // Native test seam. Production keeps the default 60-second interval.
    void setClipboardClearDelayForTests(int milliseconds);

signals:
    void activeChanged();
    void purposeChanged();
    void recoveryKeyChanged();
    void copyStateChanged();

private:
    static QString purposeName(AccountRecoveryKeyPurpose purpose);
    void clearRecoveryKeyMemory();
    void setCopyState(const QString &state);
    void clearClipboardIfUnchanged();

    AccountSensitiveClipboard *m_clipboard = nullptr;
    QString m_recoveryKey;
    AccountRecoveryKeyPurpose m_purpose =
        AccountRecoveryKeyPurpose::AccountCreated;
    QString m_copyState = QStringLiteral("idle");
    QByteArray m_clipboardDigest;
    bool m_active = false;
    int m_clipboardClearDelayMs = 60 * 1000;
    QTimer m_clipboardTimer;
};
