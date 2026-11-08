#pragma once

#include <QHostAddress>
#include <QNetworkRequest>
#include <QUrl>

#include <functional>

class QObject;

namespace NuvioNetwork {
using Resolution = std::function<void(const QHostAddress &, const QString &)>;
using FetchCompletion = std::function<void(const QByteArray &, const QUrl &, const QString &)>;

bool publicUrl(const QUrl &url, bool httpsOnly = false);
void resolve(const QUrl &url, QObject *context, Resolution complete);
void pin(QNetworkRequest &request, const QHostAddress &address);
void getRepositoryAsset(const QUrl &url, qsizetype maxBytes, QObject *context,
                        FetchCompletion complete);
}
