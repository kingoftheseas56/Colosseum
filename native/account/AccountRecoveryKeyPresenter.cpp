// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountRecoveryKeyPresenter.h"

#include "WindowsAccountSensitiveClipboard.h"

#include <QtGlobal>

AccountRecoveryKeyPresenter::AccountRecoveryKeyPresenter(
    AccountSensitiveClipboard *clipboard,
    QObject *parent)
    : QObject(parent),
      m_clipboard(clipboard) {
    Q_ASSERT(m_clipboard);

    setObjectName(
        QStringLiteral("accountRecoveryKeyPresenter"));

    m_clipboardTimer.setSingleShot(true);
    connect(
        &m_clipboardTimer,
        &QTimer::timeout,
        this,
        &AccountRecoveryKeyPresenter::clearClipboardIfUnchanged);
}

AccountRecoveryKeyPresenter::~AccountRecoveryKeyPresenter() {
    clearRecoveryKeyMemory();
    m_clipboardDigest.fill('\0');
    m_clipboardDigest.clear();
}

bool AccountRecoveryKeyPresenter::active() const {
    return m_active;
}

QString AccountRecoveryKeyPresenter::purpose() const {
    return purposeName(m_purpose);
}

QString AccountRecoveryKeyPresenter::recoveryKey() const {
    return m_recoveryKey;
}

QString AccountRecoveryKeyPresenter::copyState() const {
    return m_copyState;
}

bool AccountRecoveryKeyPresenter::presentRecoveryKey(
    const QString &recoveryKey,
    AccountRecoveryKeyPurpose purpose) {
    if (recoveryKey.trimmed().isEmpty())
        return false;

    const bool purposeChangedValue =
        m_purpose != purpose;
    const bool wasActive = m_active;

    clearRecoveryKeyMemory();
    m_recoveryKey = recoveryKey;
    m_purpose = purpose;
    m_active = true;
    setCopyState(QStringLiteral("idle"));

    if (purposeChangedValue)
        emit purposeChanged();
    emit recoveryKeyChanged();
    if (!wasActive)
        emit activeChanged();

    return true;
}

bool AccountRecoveryKeyPresenter::copyRecoveryKey() {
    if (!m_active || m_recoveryKey.isEmpty())
        return false;

    if (!m_clipboard->copyRecoveryKey(m_recoveryKey)) {
        setCopyState(QStringLiteral("failed"));
        return false;
    }

    m_clipboardDigest =
        WindowsAccountSensitiveClipboard::textDigest(
            m_recoveryKey);
    m_clipboardTimer.start(m_clipboardClearDelayMs);
    setCopyState(QStringLiteral("copied"));
    return true;
}

void AccountRecoveryKeyPresenter::dismiss() {
    if (!m_active)
        return;

    clearRecoveryKeyMemory();
    m_active = false;
    setCopyState(QStringLiteral("idle"));

    emit recoveryKeyChanged();
    emit activeChanged();
}

void AccountRecoveryKeyPresenter::setClipboardClearDelayForTests(
    int milliseconds) {
    m_clipboardClearDelayMs =
        qMax(0, milliseconds);
}

QString AccountRecoveryKeyPresenter::purposeName(
    AccountRecoveryKeyPurpose purpose) {
    switch (purpose) {
    case AccountRecoveryKeyPurpose::AccountCreated:
        return QStringLiteral("accountCreated");
    case AccountRecoveryKeyPurpose::PasswordRecovered:
        return QStringLiteral("passwordRecovered");
    case AccountRecoveryKeyPurpose::DeviceChallengeRecovered:
        return QStringLiteral("deviceChallengeRecovered");
    case AccountRecoveryKeyPurpose::ManualReplacement:
        return QStringLiteral("manualReplacement");
    }

    return QStringLiteral("accountCreated");
}

void AccountRecoveryKeyPresenter::clearRecoveryKeyMemory() {
    if (!m_recoveryKey.isEmpty())
        m_recoveryKey.fill(QLatin1Char('\0'));
    m_recoveryKey.clear();
}

void AccountRecoveryKeyPresenter::setCopyState(
    const QString &state) {
    if (m_copyState == state)
        return;
    m_copyState = state;
    emit copyStateChanged();
}

void AccountRecoveryKeyPresenter::clearClipboardIfUnchanged() {
    if (m_clipboardDigest.isEmpty())
        return;

    m_clipboard->clearIfTextMatchesDigest(
        m_clipboardDigest);

    m_clipboardDigest.fill('\0');
    m_clipboardDigest.clear();
}
