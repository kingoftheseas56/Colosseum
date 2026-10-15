// Format an addon-supplied proxy-header map into mpv's `http-header-fields` option value.
// Kept as a free function in its own header so it is unit-testable without an mpv instance
// (tests/auto/player/tst_http_header_fields.cpp). Theatre House HTTP Source, slice 1.
#ifndef COLOSSEUM_HTTP_HEADER_FIELDS_H
#define COLOSSEUM_HTTP_HEADER_FIELDS_H

#include <QChar>
#include <QLatin1Char>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// One "Field: value" entry per header. Returned as a QStringList on purpose: MpvQt marshals a
// string list to an MPV_FORMAT_NODE_ARRAY (one array element per entry), NOT a flat string, so a
// comma inside a value ("a,b") stays one header instead of being split into two bogus ones — the
// flat comma-joined form mpv also accepts would mis-split it.
//
// SECURITY — the map originates in third-party addon JSON (stream.behaviorHints.proxyHeaders).
// ffmpeg joins these entries with CRLF into the raw HTTP request, so a value carrying "\r\n"
// could inject extra headers or smuggle a request line, and a key carrying ':' or internal
// whitespace corrupts the field. Any entry that fails these checks is DROPPED, never
// sanitized-and-sent — a header we cannot vouch for does not reach the wire.
inline QStringList httpHeaderFieldsList(const QVariantMap &headers)
{
    QStringList out;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
        const QString key = it.key().trimmed();
        const QString value = it.value().toString().trimmed();
        if (key.isEmpty())
            continue;
        // A field name is an HTTP token: no ':' (the field separator), no CR/LF, no whitespace.
        if (key.contains(QLatin1Char(':')) || key.contains(QLatin1Char('\r')) || key.contains(QLatin1Char('\n')))
            continue;
        bool keyHasSpace = false;
        for (const QChar ch : key) {
            if (ch.isSpace()) { keyHasSpace = true; break; }
        }
        if (keyHasSpace)
            continue;
        // A value must not carry CR/LF (header / request-line injection).
        if (value.contains(QLatin1Char('\r')) || value.contains(QLatin1Char('\n')))
            continue;
        out << key + QStringLiteral(": ") + value;
    }
    return out;
}

#endif // COLOSSEUM_HTTP_HEADER_FIELDS_H
