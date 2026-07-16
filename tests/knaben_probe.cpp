// In-process Cloudflare/results gate for knaben.org — a meta-search aggregator
// candidate for the Tankorent indexer list. curl clearing Cloudflare proves
// NOTHING about our app's network path: CF's challenge verdict keys on the TLS
// (JA3) + HTTP/2 ALPN fingerprint, and Qt's QNetworkAccessManager fingerprints
// differently than curl. That mismatch is exactly what hid 1337x's CF wall on
// TB2. So this probe hits the knaben API through the SAME Qt network stack the
// live indexers use (QNAM + schannel TLS + the app's UA) and reports whether we
// get real JSON hits back or a Cloudflare challenge page.
//
//   Usage:  knaben_probe.exe [query]      (default query: "one piece")
//   Exit 0 = JSON with >=1 usable-infohash hit; 1 = blocked/empty; 2 = timeout.
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString query = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("one piece");

    QNetworkAccessManager nam;

    QNetworkRequest req(QUrl(QStringLiteral("https://api.knaben.org/v1")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // Match the live indexers' UA (PirateBayIndexer.cpp) so the fingerprint we
    // present to Cloudflare is representative of the real search path.
    req.setHeader(QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko)");
    req.setRawHeader("Accept", "application/json,*/*");
    req.setTransferTimeout(20000);

    QJsonObject body;
    body["search_type"] = QStringLiteral("score");
    body["search_field"] = QStringLiteral("title");
    body["query"] = query;
    body["order_by"] = QStringLiteral("seeders");
    body["order_direction"] = QStringLiteral("desc");
    body["size"] = 15;
    body["hide_unsafe"] = true;   // malware-flagged rows out
    body["hide_xxx"] = true;      // adult out at the source (data-layer refusal)
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QObject::connect(&nam, &QNetworkAccessManager::finished, &app, [&](QNetworkReply* reply) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString ctype = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const QByteArray raw = reply->readAll();
        const QByteArray server = reply->rawHeader("Server");
        const QByteArray cfRay = reply->rawHeader("CF-RAY");

        std::cout << "[knaben-probe] HTTP " << status
                  << " | content-type: " << ctype.toStdString()
                  << " | server: " << (server.isEmpty() ? "(none)" : server.toStdString())
                  << " | cf-ray: " << (cfRay.isEmpty() ? "(none)" : cfRay.toStdString()) << "\n";

        if (reply->error() != QNetworkReply::NoError && raw.isEmpty()) {
            std::cout << "[knaben-probe] FAIL network error: "
                      << reply->errorString().toStdString() << "\n";
            reply->deleteLater();
            app.exit(1);
            return;
        }

        // A genuine API response is JSON; a CF managed challenge is text/html.
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            const bool looksCf = raw.contains("Just a moment")
                || raw.contains("challenge-platform")
                || raw.contains("cf-browser-verification")
                || ctype.contains("text/html");
            std::cout << "[knaben-probe] FAIL not JSON ("
                      << (looksCf ? "Cloudflare challenge / HTML wall" : "unparseable body")
                      << "). First 200 bytes:\n"
                      << raw.left(200).toStdString() << "\n";
            reply->deleteLater();
            app.exit(1);
            return;
        }

        const QJsonArray hits = doc.object().value("hits").toArray();
        int shown = 0, withHash = 0;
        for (const QJsonValue& v : hits) {
            const QJsonObject o = v.toObject();
            const QString hash = o.value("hash").toString();
            if (hash.size() == 40) ++withHash;
            if (shown < 6) {
                std::cout << "  [hit] seeders=" << o.value("seeders").toInt()
                          << " hash=" << (hash.size() == 40 ? hash.left(12).toStdString() + "..." : "(no 40-hex)")
                          << " src=" << o.value("tracker").toString().toStdString()
                          << " | " << o.value("title").toString().left(70).toStdString() << "\n";
                ++shown;
            }
        }
        const bool ok = !hits.isEmpty() && withHash > 0;
        std::cout << "[knaben-probe] " << (ok ? "PASS" : "FAIL")
                  << " — hits=" << hits.size() << " usable-infohash=" << withHash << "\n";
        reply->deleteLater();
        app.exit(ok ? 0 : 1);
    });

    // Hard backstop so the probe never hangs (also catches the Qt/Windows IPv6
    // black-hole stall — a timeout here means the real indexer would need the
    // IPv4-pinned NAM factory).
    QTimer::singleShot(30000, &app, [&]() {
        std::cout << "[knaben-probe] FAIL timeout (30s) — possible CF stall / IPv6 black hole\n";
        app.exit(2);
    });

    nam.post(req, payload);
    return app.exec();
}
