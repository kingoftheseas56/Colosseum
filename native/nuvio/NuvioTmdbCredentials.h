#pragma once

#include <QByteArray>

class QNetworkRequest;

namespace NuvioTmdb {
struct Credential {
    QByteArray secret;
    bool bearer = false;
};

enum class Authorization { Unchanged, Attached, Missing };

inline constexpr auto placeholder = "__COLOSSEUM_TMDB_KEY__";
const char *bundledKey();
Credential loadCredential(const char *buildKey);
Authorization authorize(QNetworkRequest &request, const Credential &credential);
}
