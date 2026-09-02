// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "WindowsAccountSensitiveClipboard.h"

#include <QCryptographicHash>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>
#endif

namespace {
void clearQString(QString &value) {
    if (!value.isEmpty())
        value.fill(QLatin1Char('\0'));
    value.clear();
}

#ifdef Q_OS_WIN
class ClipboardGuard {
public:
    ClipboardGuard()
        : m_open(OpenClipboard(nullptr) != FALSE) {}

    ~ClipboardGuard() {
        if (m_open)
            CloseClipboard();
    }

    bool isOpen() const {
        return m_open;
    }

private:
    bool m_open = false;
};

bool setClipboardBytes(
    UINT format,
    const void *bytes,
    SIZE_T byteCount) {
    if (format == 0 || !bytes || byteCount == 0)
        return false;

    HGLOBAL allocation = GlobalAlloc(
        GMEM_MOVEABLE,
        byteCount);
    if (!allocation)
        return false;

    void *destination = GlobalLock(allocation);
    if (!destination) {
        GlobalFree(allocation);
        return false;
    }

    std::memcpy(destination, bytes, byteCount);
    GlobalUnlock(allocation);

    if (!SetClipboardData(format, allocation)) {
        GlobalFree(allocation);
        return false;
    }

    // Windows owns allocation after a successful SetClipboardData.
    return true;
}

bool setRegisteredDword(
    const wchar_t *name,
    DWORD value) {
    const UINT format = RegisterClipboardFormatW(name);
    if (format == 0)
        return false;

    return setClipboardBytes(
        format,
        &value,
        sizeof(value));
}

bool setRegisteredMarker(const wchar_t *name) {
    const DWORD marker = 0;
    return setRegisteredDword(name, marker);
}
#endif
}

QByteArray WindowsAccountSensitiveClipboard::textDigest(
    const QString &text) {
    return QCryptographicHash::hash(
        text.toUtf8(),
        QCryptographicHash::Sha256);
}

bool WindowsAccountSensitiveClipboard::copyRecoveryKey(
    const QString &recoveryKey) {
    if (recoveryKey.isEmpty())
        return false;

#ifdef Q_OS_WIN
    ClipboardGuard guard;
    if (!guard.isOpen())
        return false;

    if (!EmptyClipboard())
        return false;

    static_assert(
        sizeof(wchar_t) == sizeof(char16_t),
        "Windows clipboard text must be UTF-16.");

    const SIZE_T textBytes =
        static_cast<SIZE_T>(recoveryKey.size() + 1)
        * sizeof(char16_t);

    if (!setClipboardBytes(
            CF_UNICODETEXT,
            recoveryKey.utf16(),
            textBytes)) {
        EmptyClipboard();
        return false;
    }

    // Windows recognizes these registered format names specially:
    // - the monitor-processing marker excludes the whole item from history
    //   and cloud synchronization;
    // - the DWORD zero formats independently deny history and cloud upload.
    // Treat any failure as a failed copy so the UI cannot claim that a
    // recovery key was copied with protections that were not actually set.
    const bool excluded =
        setRegisteredMarker(
            L"ExcludeClipboardContentFromMonitorProcessing")
        && setRegisteredDword(
            L"CanIncludeInClipboardHistory",
            0)
        && setRegisteredDword(
            L"CanUploadToCloudClipboard",
            0);

    if (!excluded) {
        EmptyClipboard();
        return false;
    }

    return true;
#else
    Q_UNUSED(recoveryKey);
    return false;
#endif
}

bool WindowsAccountSensitiveClipboard::clearIfTextMatchesDigest(
    const QByteArray &sha256Digest) {
    if (sha256Digest.size() != 32)
        return false;

#ifdef Q_OS_WIN
    ClipboardGuard guard;
    if (!guard.isOpen())
        return false;

    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (!handle)
        return false;

    const wchar_t *raw =
        static_cast<const wchar_t *>(GlobalLock(handle));
    if (!raw)
        return false;

    QString current = QString::fromWCharArray(raw);
    GlobalUnlock(handle);

    const QByteArray currentDigest = textDigest(current);
    clearQString(current);

    if (currentDigest != sha256Digest)
        return false;

    return EmptyClipboard() != FALSE;
#else
    Q_UNUSED(sha256Digest);
    return false;
#endif
}
