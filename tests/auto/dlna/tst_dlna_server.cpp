// tst_dlna_server - Arc 71. Drives the real DlnaServer end to end on the loopback interface
// (never the LAN adapter): SSDP M-SEARCH discovery, description identity, ContentDirectory
// Browse (tree, metadata, paging, errors), media serving through pupnp's vdir (Range
// mid-file and past the 2^31 and 2^32 boundaries on a 5 GiB sparse file, HEAD, DLNA headers, epub
// download, cover art, subtitles), GENA subscribe + initial and change NOTIFYs, mid-stream
// revocation, stop (port closed) and restart (UDN + port reuse). Also the review hardening:
// 64-bit Browse paging, XML-illegal titles, files that change under a promised length, the
// cover/subtitle serving rules, sharing that resumes by itself when the network returns, and a
// stalled client that must not pin a worker. The snapshot provider is a fake; everything else
// is the shipped server code. Qt Test; no GUI; loopback only.

#include "dlna/DlnaContentDirectory.h"
#include "dlna/DlnaMediaFiles.h"
#include "dlna/DlnaNetwork.h"
#include "dlna/DlnaServer.h"
#include "dlna/DlnaSnapshot.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtNetwork>
#include <QtTest>

#include <atomic>
#include <mutex>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>
#include <io.h>
#endif

namespace {

// The injectable snapshot lane: what DlnaLibrary does in production, hand-built here.
class FakeProvider
{
public:
    std::shared_ptr<const DlnaSnapshot> snapshot() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_snapshot;
    }
    void publish(std::shared_ptr<const DlnaSnapshot> next)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_snapshot = std::move(next);
    }

private:
    mutable std::mutex m_mutex;
    std::shared_ptr<const DlnaSnapshot> m_snapshot = std::make_shared<DlnaSnapshot>();
};

QString writePatterned(const QString& path, qint64 size, qint64 patternAt, const QByteArray& pattern)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    QByteArray data(int(size), 0);
    for (int i = 0; i < int(size); i++)
        data[i] = char((i * 7 + 13) & 0xff);
    data.replace(int(patternAt), pattern.size(), pattern);
    f.write(data);
    f.close();
    return QFileInfo(path).absoluteFilePath();
}

// Mark an open file sparse so resize() + a write at a far offset costs no disk (NTFS would
// otherwise zero-fill every byte below the write). Non-Windows filesystems are sparse by default.
bool markSparse(QFile& f)
{
#ifdef Q_OS_WIN
    const HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(f.handle()));
    DWORD bytes = 0;
    return h != INVALID_HANDLE_VALUE
           && DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytes, nullptr);
#else
    Q_UNUSED(f);
    return true;
#endif
}

// Accepts GENA NOTIFY deliveries like a real control point would.
class NotifyListener : public QObject
{
public:
    explicit NotifyListener(QObject* parent = nullptr) : QObject(parent)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket* s = m_server.nextPendingConnection();
            connect(s, &QTcpSocket::disconnected, s, &QTcpSocket::deleteLater);
            connect(s, &QTcpSocket::readyRead, s, [this, s] { consume(s); });
            consume(s);
        });
    }
    bool start() { return m_server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return m_server.serverPort(); }
    QStringList events; // one full NOTIFY property-set body per event

private:
    void consume(QTcpSocket* s)
    {
        QByteArray& buf = m_bufs[s];
        buf += s->readAll();
        int hEnd;
        while ((hEnd = buf.indexOf("\r\n\r\n")) >= 0) {
            const QByteArray head = buf.left(hEnd).toLower();
            const int cl = head.indexOf("content-length:");
            int len = 0;
            if (cl >= 0) {
                const int eol = head.indexOf('\r', cl);
                len = head.mid(cl + 15, eol < 0 ? -1 : eol - cl - 15).trimmed().toInt();
            }
            if (buf.size() < hEnd + 4 + len)
                return; // body still arriving
            const QByteArray body = buf.mid(hEnd + 4, len);
            buf.remove(0, hEnd + 4 + len);
            if (head.startsWith("notify") && !body.isEmpty()) {
                events.append(QString::fromUtf8(body));
                s->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
            }
        }
    }
    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_bufs;
};

// Fixture: two movies (one with a cover, one 5 GiB sparse), a show with two episodes plus a
// sibling .srt, a book, an audiobook with two files. Container shapes mirror DlnaLibrary.
struct Fixture {
    QString movie1, big, ep1, ep2, srt, epub, cover, ab1, ab2;
    QString movie1Id = QStringLiteral("i0000000000000001");
    QString bigId = QStringLiteral("i0000000000000002");
    QString ep1Id = QStringLiteral("i0000000000000003");
    QString ep2Id = QStringLiteral("i0000000000000004");
    QString bookId = QStringLiteral("i0000000000000005");
    QString ab1Id = QStringLiteral("i0000000000000006");
    QString ab2Id = QStringLiteral("i0000000000000007");
    QString showsId = QStringLiteral("t-shows");
    QString showCid = QStringLiteral("c0000000000000sh");
    QString seasonCid = QStringLiteral("c0000000000000s1");
    // The big file is 5 GiB so a Range crosses BOTH 32-bit boundaries: 3,000,000,000 (above
    // 2^31, still below 2^32 - where a zero-extended 32-bit offset happens to look right) and
    // 4,500,000,000 (above 2^32 - where any 32-bit off_t in pupnp truncates).
    static constexpr qint64 kBigSize = 5368709120LL;
    QByteArray bigPattern = QByteArrayLiteral("BIGFILE-AT-3000000000");  // 21 bytes at 3e9
    QByteArray bigPattern2 = QByteArrayLiteral("BIGFILE-AT-4500000000"); // 21 bytes at 4.5e9
    QByteArray midPattern = QByteArrayLiteral("MIDFILE-RANGE-PATTERN"); // 21 bytes at 1000

    void build(const QString& dir)
    {
        movie1 = writePatterned(dir + "/movie1.mp4", 128 * 1024, 1000, midPattern);
        big = dir + "/big-movie.mkv";
        {
            // 5 GiB sparse: marked sparse first, then SetEndOfFile via resize(); only the two
            // pattern writes allocate clusters.
            QFile f(big);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QVERIFY2(markSparse(f), "could not mark the big file sparse");
            QVERIFY(f.resize(kBigSize));
            QVERIFY(f.seek(3000000000LL));
            QCOMPARE(f.write(bigPattern), qint64(bigPattern.size()));
            QVERIFY(f.seek(4500000000LL));
            QCOMPARE(f.write(bigPattern2), qint64(bigPattern2.size()));
            f.close();
        }
        ep1 = writePatterned(dir + "/Show S01E01.mkv", 64 * 1024, 0, {});
        ep2 = writePatterned(dir + "/Show S01E02.mkv", 64 * 1024, 0, {});
        {
            QFile f(dir + "/Show S01E02.srt");
            QVERIFY(f.open(QIODevice::WriteOnly));
            QVERIFY(f.write("1\n00:00:01,000 --> 00:00:02,000\nhello\n") > 0);
            f.close();
            srt = QFileInfo(f).absoluteFilePath();
        }
        {
            QFile f(dir + "/book.epub");
            QVERIFY(f.open(QIODevice::WriteOnly));
            QVERIFY(f.write("PK\x03" "\x04" "fake-epub") > 0);
            f.close();
            epub = QFileInfo(f).absoluteFilePath();
        }
        {
            QFile f(dir + "/cover.jpg");
            QVERIFY(f.open(QIODevice::WriteOnly));
            QVERIFY(f.write("\xff\xd8\xff" "\xe0" "fake-jpeg") > 0);
            f.close();
            cover = QFileInfo(f).absoluteFilePath();
        }
        ab1 = writePatterned(dir + "/audiobook-01.mp3", 32 * 1024, 0, {});
        ab2 = writePatterned(dir + "/audiobook-02.mp3", 32 * 1024, 0, {});
    }

    DlnaItem item(const QString& id, const QString& parent, const QString& title,
                  const QString& cls, const QString& path, const QString& mime) const
    {
        const QFileInfo fi(path);
        DlnaItem it;
        it.id = id;
        it.parentId = parent;
        it.title = title;
        it.upnpClass = cls;
        it.filePath = QFileInfo(path).absoluteFilePath();
        it.mime = mime;
        it.size = fi.size();
        it.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
        return it;
    }

    std::shared_ptr<DlnaSnapshot> snapshot1() const
    {
        auto snap = std::make_shared<DlnaSnapshot>();
        auto addContainer = [&](const QString& id, const QString& parent, const QString& title) {
            DlnaContainer c;
            c.id = id;
            c.parentId = parent;
            c.title = title;
            c.upnpClass = QStringLiteral("object.container.storageFolder");
            snap->containers.insert(id, c);
            snap->containers[parent].childContainerIds.append(id);
        };
        auto addItem = [&](const DlnaItem& it) {
            snap->items.insert(it.id, it);
            snap->containers[it.parentId].childItemIds.append(it.id);
        };

        DlnaContainer root;
        root.id = QStringLiteral("0");
        root.parentId = QStringLiteral("-1");
        root.title = QStringLiteral("Colosseum");
        root.upnpClass = QStringLiteral("object.container.storageFolder");
        snap->containers.insert(root.id, root);

        addContainer(QStringLiteral("t-movies"), QStringLiteral("0"), QStringLiteral("Movies"));
        DlnaItem m1 = item(movie1Id, QStringLiteral("t-movies"), QStringLiteral("Alpha Movie"),
                           QStringLiteral("object.item.videoItem.movie"), movie1,
                           QStringLiteral("video/mp4"));
        m1.year = 2024;
        m1.genres = QStringLiteral("Drama");
        m1.durationSec = 5400.0;
        m1.coverPath = cover;
        addItem(m1);
        addItem(item(bigId, QStringLiteral("t-movies"), QStringLiteral("Huge Movie"),
                     QStringLiteral("object.item.videoItem.movie"), big,
                     QStringLiteral("video/x-matroska")));

        addContainer(showsId, QStringLiteral("0"), QStringLiteral("Shows"));
        addContainer(showCid, showsId, QStringLiteral("The Show"));
        addContainer(seasonCid, showCid, QStringLiteral("Season 1"));
        DlnaItem e1 = item(ep1Id, seasonCid, QStringLiteral("Pilot"),
                           QStringLiteral("object.item.videoItem"), ep1,
                           QStringLiteral("video/x-matroska"));
        e1.season = 1;
        e1.episode = 1;
        e1.seriesTitle = QStringLiteral("The Show");
        addItem(e1);
        DlnaItem e2 = item(ep2Id, seasonCid, QStringLiteral("Second"),
                           QStringLiteral("object.item.videoItem"), ep2,
                           QStringLiteral("video/x-matroska"));
        e2.season = 1;
        e2.episode = 2;
        e2.seriesTitle = QStringLiteral("The Show");
        addItem(e2);

        addContainer(QStringLiteral("t-books"), QStringLiteral("0"), QStringLiteral("Books"));
        DlnaItem b = item(bookId, QStringLiteral("t-books"), QStringLiteral("A Book"),
                          QStringLiteral("object.item.textItem"), epub,
                          QStringLiteral("application/epub+zip"));
        b.author = QStringLiteral("Author Name");
        addItem(b);

        addContainer(QStringLiteral("t-audiobooks"), QStringLiteral("0"),
                     QStringLiteral("Audiobooks"));
        addContainer(QStringLiteral("c0000000000000ab"), QStringLiteral("t-audiobooks"),
                     QStringLiteral("An Audiobook"));
        addItem(item(ab1Id, QStringLiteral("c0000000000000ab"), QStringLiteral("Part 1"),
                     QStringLiteral("object.item.audioItem.audioBook"), ab1,
                     QStringLiteral("audio/mpeg")));
        addItem(item(ab2Id, QStringLiteral("c0000000000000ab"), QStringLiteral("Part 2"),
                     QStringLiteral("object.item.audioItem.audioBook"), ab2,
                     QStringLiteral("audio/mpeg")));

        snap->typeOrder = QStringList{QStringLiteral("Movies"), QStringLiteral("Shows"),
                                      QStringLiteral("Books"), QStringLiteral("Audiobooks")};
        snap->itemCount = snap->items.size();
        snap->systemUpdateId = 1;
        return snap;
    }
};

#ifdef Q_OS_WIN
// The spike's proven M-SEARCH recipe: multicast IF pinned to loopback, 239.255.255.250:1900.
QString msearchDiscover(int attempts = 6)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return {};
    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    in_addr mcastIf{};
    mcastIf.S_un.S_addr = htonl(0x7f000001u);
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&mcastIf, sizeof(mcastIf));
    DWORD ttl = 2;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(1900);
    dest.sin_addr.S_un.S_addr = inet_addr("239.255.255.250");
    const char* msg = "M-SEARCH * HTTP/1.1\r\n"
                      "HOST: 239.255.255.250:1900\r\n"
                      "MAN: \"ssdp:discover\"\r\n"
                      "MX: 1\r\n"
                      "ST: upnp:rootdevice\r\n"
                      "\r\n";
    for (int i = 0; i < attempts; i++) {
        sendto(s, msg, int(strlen(msg)), 0, (sockaddr*)&dest, sizeof(dest));
        char buf[4096];
        for (int r = 0; r < 8; r++) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(s, &fds);
            timeval tv{2, 0};
            if (select(0, &fds, nullptr, nullptr, &tv) <= 0)
                break;
            sockaddr_in from{};
            int fromLen = sizeof(from);
            const int n = recvfrom(s, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fromLen);
            if (n <= 0)
                break;
            buf[n] = '\0';
            const QString reply = QString::fromLatin1(buf).toLower();
            const int loc = reply.indexOf(QStringLiteral("location:"));
            if (loc >= 0) {
                const int end = reply.indexOf(QStringLiteral("\r\n"), loc);
                closesocket(s);
                return reply.mid(loc + 9, end - loc - 9).trimmed();
            }
        }
    }
    closesocket(s);
    return {};
}
#endif

// The container id whose <dc:title> is `title` — opaque ids resolved like a real client.
QString containerIdForTitle(const QString& didl, const QString& title)
{
    const int t = didl.indexOf(QStringLiteral("<dc:title>") + title
                               + QStringLiteral("</dc:title>"));
    if (t < 0)
        return {};
    const int c = didl.lastIndexOf(QStringLiteral("<container id=\""), t);
    if (c < 0)
        return {};
    const int q = didl.indexOf(QLatin1Char('"'), c + 15);
    return didl.mid(c + 15, q - c - 15);
}

} // namespace

class tst_dlna_server : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void adapterSelection();
    void holdForExternalChecker();
    void discoverAndDescribe();
    void deviceDescriptionAndScpds();
    void registrar();
    void didlNamespacesAndWellFormedness();
    void browseTree();
    void browsePagingAndErrors();
    void browseHugeIndicesAndMissingArgs();
    void browseSortOrder();
    void xmlHostileTitles();
    void mediaHttp();
    void bigFileRanges();
    void epubDownload();
    void subtitleServing();
    void changedFileIsNotServedTruncated();
    void coverArtRules();
    void genaEvents();
    void revocation();
    void stopClosesPort();
    void restartReusesUdnAndPort();
    void resumesAfterNetworkLoss();
    void stalledClientFreesWorker();
    void cleanupTestCase();

private:
    struct Reply {
        int status = 0;
        QMap<QByteArray, QByteArray> headers; // lowercased names
        QByteArray body;
    };
    Reply http(const QString& method, const QUrl& url,
               const QList<QPair<QByteArray, QByteArray>>& headers = {});
    struct SoapReply {
        bool ok = false;
        int faultCode = 0;
        QMap<QString, QString> args;
        QString arg(const QString& name) const { return args.value(name); }
    };
    SoapReply soap(const QString& serviceType, const QString& action,
                   const QList<QPair<QString, QString>>& inArgs);

    SoapReply browseDirectChildren(const QString& objectId);
    SoapReply browseMetadata(const QString& objectId);
    SoapReply browseWith(const QString& objectId, const QString& flag, const QString& start,
                         const QString& count, const QString& sort = QString());

    QTemporaryDir m_tmp;
    Fixture m_fx;
    FakeProvider m_provider;
    QSettings* m_settings = nullptr;
    DlnaServer* m_server = nullptr;
    QString m_baseUrl;
    QNetworkAccessManager m_nam;
    quint16 m_boundPort = 0;
    QString m_udn;
};

// ── fixture ──────────────────────────────────────────────────────────────────────────────

void tst_dlna_server::initTestCase()
{
#ifdef Q_OS_WIN
    WSADATA wsa;
    QVERIFY(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
#endif
    QVERIFY(m_tmp.isValid());
    m_fx.build(m_tmp.path());
    m_provider.publish(m_fx.snapshot1());

    const auto loopback = DlnaNetwork::loopbackAdapter();
    if (!loopback)
        QSKIP("no loopback adapter on this machine");
    QCOMPARE(loopback->ip, QStringLiteral("127.0.0.1"));

    m_settings = new QSettings(m_tmp.path() + "/dlna-settings.ini", QSettings::IniFormat, this);
    m_server = new DlnaServer(this);
    m_server->setSettings(m_settings);
    m_server->setSnapshotProvider([this] { return m_provider.snapshot(); });
    m_server->setForcedInterface(*loopback);
    m_server->setEnabled(true);
    QTRY_COMPARE_WITH_TIMEOUT(m_server->running(), true, 15000);
    QVERIFY(m_server->error().isEmpty());
    QVERIFY(!m_server->busy());
    QVERIFY(m_server->friendlyName().startsWith(QStringLiteral("Colosseum on ")));

    // The first publish (DlnaLibrary::snapshotChanged in production).
    m_server->handleSnapshotChanged();
    QCOMPARE(m_server->shareTypes(),
             (QStringList{QStringLiteral("Movies"), QStringLiteral("Shows"),
                         QStringLiteral("Books"), QStringLiteral("Audiobooks")}));
    QCOMPARE(m_server->itemCount(), 7);
    QVERIFY(m_server->networkLabel().contains(QStringLiteral("127.0.0.1")));

    m_udn = m_settings->value(QStringLiteral("dlnaSharing/uuid")).toString();
    QVERIFY(m_udn.startsWith(QStringLiteral("uuid:")));
    m_boundPort = quint16(m_settings->value(QStringLiteral("dlnaSharing/port")).toUInt());
    QVERIFY(m_boundPort != 0);
    m_baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(m_boundPort);
}

// ── HTTP + SOAP plumbing ─────────────────────────────────────────────────────────────────

tst_dlna_server::Reply tst_dlna_server::http(const QString& method, const QUrl& url,
                                             const QList<QPair<QByteArray, QByteArray>>& headers)
{
    QNetworkRequest req(url);
    for (const auto& h : headers)
        req.setRawHeader(h.first, h.second);
    QNetworkReply* reply = method == QLatin1String("GET")
                               ? m_nam.get(req)
                               : m_nam.sendCustomRequest(req, method.toLatin1());
    Reply out;
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();
    out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QList<QNetworkReply::RawHeaderPair> pairs = reply->rawHeaderPairs();
    for (const auto& p : pairs)
        out.headers.insert(p.first.toLower(), p.second);
    out.body = reply->readAll();
    reply->deleteLater();
    return out;
}

tst_dlna_server::SoapReply tst_dlna_server::soap(const QString& serviceType, const QString& action,
                                                 const QList<QPair<QString, QString>>& inArgs)
{
    QString body = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:%1 xmlns:u=\"%2\">").arg(action, serviceType);
    for (const auto& a : inArgs)
        body += QStringLiteral("<%1>%2</%1>")
                    .arg(a.first, a.second.toHtmlEscaped()); // element text: & < > suffice
    body += QStringLiteral("</u:%1></s:Body></s:Envelope>").arg(action);

    QNetworkRequest req(QUrl(
        m_baseUrl
        + (serviceType.endsWith(QLatin1String("ContentDirectory:1"))
               ? QStringLiteral("/control/cd")
               : serviceType.contains(QLatin1String("X_MS_MediaReceiverRegistrar"))
                     ? QStringLiteral("/control/mrr")
                     : QStringLiteral("/control/cm"))));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/xml; charset=utf-8"));
    req.setRawHeader("SOAPACTION",
                     QStringLiteral("\"%1#%2\"").arg(serviceType, action).toLatin1());
    QNetworkReply* reply = m_nam.post(req, body.toUtf8());

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();
    const QByteArray raw = reply->readAll();
    reply->deleteLater();

    SoapReply out;
    QXmlStreamReader xml(raw);
    QString elem;
    bool sawResponse = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            elem = xml.name().toString();
            if (elem.endsWith(QLatin1String("Response")))
                sawResponse = true;
            else if (sawResponse && elem != QLatin1String("Body"))
                out.args.insert(elem, out.args.value(elem)); // empty-valued out-args still count
        } else if (xml.isEndElement())
            elem.clear();
        else if (xml.isCharacters() && !xml.isWhitespace() && !elem.isEmpty()) {
            const QString text = xml.text().toString();
            if (elem == QLatin1String("errorCode"))
                out.faultCode = text.toInt();
            else if (elem != QLatin1String("faultcode") && elem != QLatin1String("faultstring")
                     && elem != QLatin1String("errorDescription"))
                out.args.insert(elem, text);
        }
    }
    out.ok = out.faultCode == 0 && sawResponse;
    return out;
}

tst_dlna_server::SoapReply tst_dlna_server::browseDirectChildren(const QString& objectId)
{
    return soap(QStringLiteral("urn:schemas-upnp-org:service:ContentDirectory:1"),
                QStringLiteral("Browse"),
                {{QStringLiteral("ObjectID"), objectId},
                 {QStringLiteral("BrowseFlag"), QStringLiteral("BrowseDirectChildren")},
                 {QStringLiteral("Filter"), QStringLiteral("*")},
                 {QStringLiteral("StartingIndex"), QStringLiteral("0")},
                 {QStringLiteral("RequestedCount"), QStringLiteral("0")},
                 {QStringLiteral("SortCriteria"), QString()}});
}

tst_dlna_server::SoapReply tst_dlna_server::browseMetadata(const QString& objectId)
{
    return soap(QStringLiteral("urn:schemas-upnp-org:service:ContentDirectory:1"),
                QStringLiteral("Browse"),
                {{QStringLiteral("ObjectID"), objectId},
                 {QStringLiteral("BrowseFlag"), QStringLiteral("BrowseMetadata")},
                 {QStringLiteral("Filter"), QStringLiteral("*")},
                 {QStringLiteral("StartingIndex"), QStringLiteral("0")},
                 {QStringLiteral("RequestedCount"), QStringLiteral("0")},
                 {QStringLiteral("SortCriteria"), QString()}});
}

tst_dlna_server::SoapReply tst_dlna_server::browseWith(const QString& objectId,
                                                       const QString& flag, const QString& start,
                                                       const QString& count, const QString& sort)
{
    return soap(QStringLiteral("urn:schemas-upnp-org:service:ContentDirectory:1"),
                QStringLiteral("Browse"),
                {{QStringLiteral("ObjectID"), objectId},
                 {QStringLiteral("BrowseFlag"), flag},
                 {QStringLiteral("Filter"), QStringLiteral("*")},
                 {QStringLiteral("StartingIndex"), start},
                 {QStringLiteral("RequestedCount"), count},
                 {QStringLiteral("SortCriteria"), sort}});
}

// ── interface policy (read-only: nothing binds here) ─────────────────────────────────────

void tst_dlna_server::adapterSelection()
{
    const QList<DlnaNetAdapter> all = DlnaNetwork::enumerateAdapters();
    QVERIFY(!all.isEmpty());
    for (const DlnaNetAdapter& a : all)
        qInfo().noquote() << "adapter seen:" << a.ifName << "|" << a.ip << "|" << a.label;

    const auto home = DlnaNetwork::pickHomeAdapter();
    if (!home) {
        qInfo() << "no home adapter on this machine (valid: server would report 'No home network found')";
        return;
    }
    qInfo().noquote() << "home pick:" << home->ifName << "|" << home->ip << "|" << home->label;
    // Never loopback, never link-local, always RFC1918, never a virtual/tunnel adapter name,
    // and the pick must be one of the adapters pupnp could actually be asked to bind by name.
    QVERIFY(!home->ip.startsWith(QLatin1String("127.")));
    QVERIFY(!home->ip.startsWith(QLatin1String("169.254.")));
    const bool rfc1918 = home->ip.startsWith(QLatin1String("10."))
                         || home->ip.startsWith(QLatin1String("192.168."))
                         || QRegularExpression(QStringLiteral("^172\\.(1[6-9]|2[0-9]|3[01])\\."))
                                .match(home->ip).hasMatch();
    QVERIFY2(rfc1918, qPrintable(home->ip));
    // ifName is the adapter GUID (what pupnp matches); the friendly name travels in the label.
    QVERIFY2(home->ifName.startsWith(QLatin1Char('{')), qPrintable(home->ifName));
    const QString lower = home->label.toLower();
    for (const char* bad : {"vethernet", "hyper-v", "wsl", "virtualbox", "vmware", "tailscale",
                            "zerotier", "wireguard", "npcap", "loopback", "virtual", "miniport",
                            "anyconnect", "globalprotect", "hamachi", "wintun", "nordlynx",
                            "openvpn"})
        QVERIFY2(!lower.contains(QLatin1String(bad)), qPrintable(home->label));
    QVERIFY(DlnaNetwork::isPrivateIpv4(QStringLiteral("192.168.1.20")));
    QVERIFY(DlnaNetwork::isPrivateIpv4(QStringLiteral("172.31.0.9")));
    QVERIFY(!DlnaNetwork::isPrivateIpv4(QStringLiteral("172.32.0.9")));
    QVERIFY(!DlnaNetwork::isPrivateIpv4(QStringLiteral("169.254.3.4")));
    QVERIFY(!DlnaNetwork::isPrivateIpv4(QStringLiteral("8.8.8.8")));
    QVERIFY(!DlnaNetwork::isPrivateIpv4(QStringLiteral("127.0.0.1")));
    QVERIFY(!DlnaNetwork::isPrivateIpv4(QStringLiteral("10.1.2")));
    bool listed = false;
    for (const DlnaNetAdapter& a : all)
        listed = listed || (a.ifName == home->ifName && a.ip == home->ip);
    QVERIFY(listed);
}

// Optional lane for tests/tools/dlna_check.py and VLC: with DLNA_TEST_HOLD_FILE=<path> set, write
// the loopback base URL to <path> and keep serving the fixture until <path>.done appears
// (3 minutes at most). Without the variable this slot skips, so ordinary runs are unaffected.
void tst_dlna_server::holdForExternalChecker()
{
    const QString holdFile = QString::fromLocal8Bit(qgetenv("DLNA_TEST_HOLD_FILE"));
    if (holdFile.isEmpty())
        QSKIP("set DLNA_TEST_HOLD_FILE to serve the fixture to an external checker");
    {
        QFile f(holdFile);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(m_baseUrl.toUtf8());
    }
    // DLNA_TEST_REAL_MP4=<file>: also publish that real clip as "Real Clip" under Movies and
    // write its media URL as the second line - VLC needs bytes it can actually demux (the
    // fixture's movies are patterned noise). Leaves the fixture untouched when unset.
    const QString realMp4 = QString::fromLocal8Bit(qgetenv("DLNA_TEST_REAL_MP4"));
    if (!realMp4.isEmpty()) {
        QVERIFY(QFileInfo::exists(realMp4));
        auto snap = std::make_shared<DlnaSnapshot>(*m_provider.snapshot());
        const QString id = QStringLiteral("i00000000000000ff");
        snap->items.insert(id, m_fx.item(id, QStringLiteral("t-movies"), QStringLiteral("Real Clip"),
                                         QStringLiteral("object.item.videoItem.movie"), realMp4,
                                         QStringLiteral("video/mp4")));
        snap->containers[QStringLiteral("t-movies")].childItemIds.append(id);
        snap->itemCount = snap->items.size();
        snap->systemUpdateId += 1;
        m_provider.publish(snap);
        m_server->handleSnapshotChanged();
        QFile f(holdFile);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Append));
        f.write(QStringLiteral("\n%1/m/%2.mp4").arg(m_baseUrl, id).toUtf8());
    }
    const QString done = holdFile + QStringLiteral(".done");
    QElapsedTimer waited;
    waited.start();
    while (!QFileInfo::exists(done) && waited.elapsed() < 180000)
        QTest::qWait(200);
    QVERIFY2(QFileInfo::exists(done), "the external checker never signalled completion");
}

// ── discovery + description ──────────────────────────────────────────────────────────────

void tst_dlna_server::discoverAndDescribe()
{
#ifdef Q_OS_WIN
    const QString location = msearchDiscover();
    QVERIFY2(!location.isEmpty(), "M-SEARCH over loopback found no root device");
    QCOMPARE(location, m_baseUrl + QStringLiteral("/description.xml"));
#else
    QSKIP("M-SEARCH helper is winsock-specific");
#endif

    const Reply desc = http(QStringLiteral("GET"), QUrl(m_baseUrl + "/description.xml"));
    QCOMPARE(desc.status, 200);
    const QString xml = QString::fromUtf8(desc.body);
    QVERIFY(xml.contains(QStringLiteral("urn:schemas-upnp-org:device:MediaServer:1")));
    QVERIFY(xml.contains(QStringLiteral("<friendlyName>Colosseum on ")));
    QVERIFY(xml.contains(QStringLiteral("<UDN>") + m_udn + QStringLiteral("</UDN>")));
    QVERIFY(xml.contains(QStringLiteral("DMS-1.50")));
    QVERIFY(xml.contains(QStringLiteral("urn:schemas-upnp-org:service:ContentDirectory:1")));
    QVERIFY(xml.contains(QStringLiteral("urn:schemas-upnp-org:service:ConnectionManager:1")));
    QVERIFY(xml.contains(QStringLiteral("/icons/colosseum-48.png")));
    QVERIFY(!xml.contains(QStringLiteral("presentationURL")));

    const Reply icon = http(QStringLiteral("GET"), QUrl(m_baseUrl + "/icons/colosseum-48.png"));
    QCOMPARE(icon.status, 200);
    QCOMPARE(icon.headers.value("content-type"), QByteArray("image/png"));
    QVERIFY(icon.body.size() > 100);

    const Reply scpd = http(QStringLiteral("GET"), QUrl(m_baseUrl + "/scpd/cd.xml"));
    QCOMPARE(scpd.status, 200);
    QVERIFY(scpd.body.contains("Browse"));
    QVERIFY(scpd.body.contains("ContainerUpdateIDs"));
}

// What a strict control point checks before it trusts a device: the description's element
// order and required fields, DLNA identity, icons that really fetch as the advertised
// type/size, and SCPDs that are well-formed, self-consistent and evented only where the
// service type says so.
void tst_dlna_server::deviceDescriptionAndScpds()
{
    const Reply desc = http(QStringLiteral("GET"), QUrl(m_baseUrl + "/description.xml"));
    QCOMPARE(desc.status, 200);
    QVERIFY2(desc.headers.value("content-type").startsWith("text/xml"),
             desc.headers.value("content-type").constData());
    QXmlStreamReader xml(desc.body);
    QStringList deviceChildren; // element order inside <device>
    QStringList serviceTypes, scpdUrls, controlUrls, eventUrls, iconUrls;
    QList<int> iconDepths;
    QString specMinor, urlBase, dlnaDoc;
    bool dlnaDocNamespaced = false;
    QStringList path;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString n = xml.name().toString();
            if (path.size() == 2 && path.at(1) == QLatin1String("device"))
                deviceChildren << n;
            if (n == QLatin1String("X_DLNADOC")) {
                dlnaDocNamespaced =
                    xml.namespaceUri() == QLatin1String("urn:schemas-dlna-org:device-1-0");
                dlnaDoc = xml.readElementText();
            } else if (n == QLatin1String("minor") && specMinor.isEmpty()) {
                specMinor = xml.readElementText();
            } else if (n == QLatin1String("URLBase")) {
                urlBase = xml.readElementText();
            } else if (n == QLatin1String("serviceType")) {
                serviceTypes << xml.readElementText();
            } else if (n == QLatin1String("SCPDURL")) {
                scpdUrls << xml.readElementText();
            } else if (n == QLatin1String("controlURL")) {
                controlUrls << xml.readElementText();
            } else if (n == QLatin1String("eventSubURL")) {
                eventUrls << xml.readElementText();
            } else if (n == QLatin1String("url")) {
                iconUrls << xml.readElementText();
            } else if (n == QLatin1String("depth")) {
                iconDepths << xml.readElementText().toInt();
            } else {
                path << n;
            }
        } else if (xml.isEndElement()) {
            if (!path.isEmpty() && path.last() == xml.name().toString())
                path.removeLast();
        }
    }
    QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
    QCOMPARE(specMinor, QStringLiteral("0")); // no BOOTID/CONFIGID promised, none sent
    QCOMPARE(dlnaDoc, QStringLiteral("DMS-1.50"));
    QVERIFY(dlnaDocNamespaced);
    QVERIFY(urlBase.startsWith(QStringLiteral("http://")) && urlBase.endsWith(QLatin1Char('/')));
    // Schema order of the device children we emit.
    const QStringList wanted{QStringLiteral("deviceType"), QStringLiteral("friendlyName"),
                             QStringLiteral("manufacturer"), QStringLiteral("manufacturerURL"),
                             QStringLiteral("modelDescription"), QStringLiteral("modelName"),
                             QStringLiteral("modelNumber"), QStringLiteral("UDN"),
                             QStringLiteral("X_DLNADOC"), QStringLiteral("iconList"),
                             QStringLiteral("serviceList")};
    QCOMPARE(deviceChildren, wanted);
    QCOMPARE(serviceTypes.size(), 3);
    QCOMPARE(scpdUrls.size(), 3);
    QCOMPARE(controlUrls.size(), 3);
    QCOMPARE(eventUrls.size(), 3);
    QVERIFY(serviceTypes.contains(
        QStringLiteral("urn:schemas-upnp-org:service:ContentDirectory:1")));
    QVERIFY(serviceTypes.contains(
        QStringLiteral("urn:schemas-upnp-org:service:ConnectionManager:1")));
    QVERIFY(serviceTypes.contains(
        QStringLiteral("urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1")));
    QCOMPARE(iconDepths, (QList<int>{24, 24}));

    // Icons: really PNGs of the advertised size.
    const QList<QSize> sizes{QSize(48, 48), QSize(120, 120)};
    QCOMPARE(iconUrls.size(), 2);
    for (int i = 0; i < iconUrls.size(); i++) {
        const Reply icon = http(QStringLiteral("GET"), QUrl(urlBase + iconUrls.at(i).mid(1)));
        QCOMPARE(icon.status, 200);
        QCOMPARE(icon.headers.value("content-type"), QByteArray("image/png"));
        // PNG signature, then IHDR width/height (big-endian) at bytes 16..23.
        QVERIFY(icon.body.startsWith("\x89PNG\r\n\x1a\n"));
        const auto be32 = [&](int at) {
            return (quint32(quint8(icon.body[at])) << 24)
                   | (quint32(quint8(icon.body[at + 1])) << 16)
                   | (quint32(quint8(icon.body[at + 2])) << 8) | quint32(quint8(icon.body[at + 3]));
        };
        QCOMPARE(QSize(int(be32(16)), int(be32(20))), sizes.at(i));
    }

    // Every SCPD fetches as text/xml, parses, and each argument's relatedStateVariable exists.
    for (const QString& scpdUrl : std::as_const(scpdUrls)) {
        const Reply scpd = http(QStringLiteral("GET"), QUrl(m_baseUrl + scpdUrl));
        QCOMPARE(scpd.status, 200);
        QVERIFY2(scpd.headers.value("content-type").startsWith("text/xml"), qPrintable(scpdUrl));
        QVERIFY2(scpd.headers.value("content-type").contains("utf-8"), qPrintable(scpdUrl));
        QXmlStreamReader sx(scpd.body);
        QSet<QString> related;
        QMap<QString, QString> sendEvents, types;
        QString eventsAttr, currentVar;
        bool inVar = false;
        while (!sx.atEnd()) {
            sx.readNext();
            if (sx.isEndElement() && sx.name() == QLatin1String("stateVariable")) {
                inVar = false;
            } else if (sx.isStartElement()) {
                const QString n = sx.name().toString();
                if (n == QLatin1String("stateVariable")) {
                    inVar = true;
                    eventsAttr = sx.attributes().value(QLatin1String("sendEvents")).toString();
                } else if (inVar && n == QLatin1String("name")) {
                    currentVar = sx.readElementText();
                    sendEvents.insert(currentVar, eventsAttr);
                    types.insert(currentVar, QString());
                } else if (inVar && n == QLatin1String("dataType")) {
                    types[currentVar] = sx.readElementText();
                } else if (n == QLatin1String("relatedStateVariable")) {
                    related.insert(sx.readElementText());
                }
            }
        }
        QVERIFY2(!sx.hasError(), qPrintable(scpdUrl + QLatin1String(": ") + sx.errorString()));
        for (const QString& r : std::as_const(related))
            QVERIFY2(types.contains(r), qPrintable(scpdUrl + QLatin1String(" lacks ") + r));
        QStringList evented;
        for (auto it = types.constBegin(); it != types.constEnd(); ++it) {
            QVERIFY2(!it.value().isEmpty(), qPrintable(it.key()));
            if (it.key().startsWith(QLatin1String("A_ARG_TYPE_")))
                QCOMPARE(sendEvents.value(it.key()), QStringLiteral("no"));
            if (sendEvents.value(it.key()) == QLatin1String("yes"))
                evented << it.key();
        }
        evented.sort();
        if (scpdUrl.endsWith(QLatin1String("cd.xml"))) {
            QCOMPARE(types.value(QStringLiteral("SystemUpdateID")), QStringLiteral("ui4"));
            QCOMPARE(types.value(QStringLiteral("A_ARG_TYPE_Index")), QStringLiteral("ui4"));
            QCOMPARE(types.value(QStringLiteral("A_ARG_TYPE_Count")), QStringLiteral("ui4"));
            QCOMPARE(evented, (QStringList{QStringLiteral("ContainerUpdateIDs"),
                                           QStringLiteral("SystemUpdateID")}));
        } else if (scpdUrl.endsWith(QLatin1String("cm.xml"))) {
            QCOMPARE(evented, (QStringList{QStringLiteral("CurrentConnectionIDs"),
                                           QStringLiteral("SinkProtocolInfo"),
                                           QStringLiteral("SourceProtocolInfo")}));
        }
    }

    // An unknown SCPD path is a clean 404.
    QCOMPARE(http(QStringLiteral("GET"), QUrl(m_baseUrl + "/scpd/nope.xml")).status, 404);
}

// X_MS_MediaReceiverRegistrar: Kodi's constants (authorized, validated, empty registration).
void tst_dlna_server::registrar()
{
    const QString rg = QStringLiteral("urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1");
    SoapReply auth = soap(rg, QStringLiteral("IsAuthorized"),
                          {{QStringLiteral("DeviceID"), QString()}});
    QVERIFY(auth.ok);
    QCOMPARE(auth.arg(QStringLiteral("Result")), QStringLiteral("1"));
    SoapReply valid = soap(rg, QStringLiteral("IsValidated"),
                           {{QStringLiteral("DeviceID"), QStringLiteral("anything")}});
    QVERIFY(valid.ok);
    QCOMPARE(valid.arg(QStringLiteral("Result")), QStringLiteral("1"));
    SoapReply reg = soap(rg, QStringLiteral("RegisterDevice"),
                         {{QStringLiteral("RegistrationReqMsg"), QStringLiteral("AAAA")}});
    QVERIFY(reg.ok);
    QVERIFY(reg.args.contains(QStringLiteral("RegistrationRespMsg")));
    SoapReply bad = soap(rg, QStringLiteral("Nope"), {});
    QVERIFY(!bad.ok);
    QCOMPARE(bad.faultCode, 401);

    // It can be subscribed to like the other two services.
    QNetworkRequest req(QUrl(m_baseUrl + "/event/mrr"));
    req.setRawHeader("NT", "upnp:event");
    req.setRawHeader("TIMEOUT", "Second-300");
    req.setRawHeader("CALLBACK", "<http://127.0.0.1:9/none>");
    QNetworkReply* sub = m_nam.sendCustomRequest(req, "SUBSCRIBE");
    QEventLoop loop;
    connect(sub, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    QCOMPARE(sub->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    QVERIFY(sub->rawHeader("SID").startsWith("uuid:"));
    sub->deleteLater();
}

// Every DIDL the server emits must be well-formed XML with every prefix it uses bound -
// including sec: (Samsung's http://www.sec.co.kr/) - and each res must carry a four-field
// protocolInfo. A TV drops a whole page on the first violation.
void tst_dlna_server::didlNamespacesAndWellFormedness()
{
    const QStringList containers{QStringLiteral("0"), QStringLiteral("t-movies"), m_fx.showsId,
                                 m_fx.showCid, m_fx.seasonCid, QStringLiteral("t-books"),
                                 QStringLiteral("t-audiobooks"),
                                 QStringLiteral("c0000000000000ab")};
    int resSeen = 0;
    for (const QString& id : containers) {
        SoapReply r = browseDirectChildren(id);
        QVERIFY2(r.ok, qPrintable(id));
        const QString didl = r.arg(QStringLiteral("Result"));
        QXmlStreamReader xml(didl);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QLatin1String("res")) {
                const QString pi =
                    xml.attributes().value(QLatin1String("protocolInfo")).toString();
                QCOMPARE(pi.split(QLatin1Char(':')).size(), 4);
                QVERIFY(pi.startsWith(QStringLiteral("http-get:*:")));
                resSeen++;
            }
        }
        QVERIFY2(!xml.hasError(), qPrintable(id + QLatin1String(": ") + xml.errorString()));
        QVERIFY(didl.contains(QStringLiteral("xmlns:sec=\"http://www.sec.co.kr/\"")));
        QVERIFY(didl.contains(
            QStringLiteral("xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0/\"")));
    }
    QVERIFY(resSeen >= 7);

    // Subtitled episode: both Samsung spellings, in the sec: namespace, parsed through it.
    SoapReply ep = browseMetadata(m_fx.ep2Id);
    QVERIFY(ep.ok);
    QXmlStreamReader xml(ep.arg(QStringLiteral("Result")));
    QStringList captionNames;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.namespaceUri() == QLatin1String("http://www.sec.co.kr/"))
            captionNames << xml.name().toString();
    }
    QVERIFY(!xml.hasError());
    captionNames.sort();
    QCOMPARE(captionNames,
             (QStringList{QStringLiteral("CaptionInfo"), QStringLiteral("CaptionInfoEx")}));
}

// ── ContentDirectory ─────────────────────────────────────────────────────────────────────

void tst_dlna_server::browseTree()
{
    SoapReply root = browseDirectChildren(QStringLiteral("0"));
    QVERIFY(root.ok);
    QCOMPARE(root.arg(QStringLiteral("NumberReturned")), QStringLiteral("4"));
    QCOMPARE(root.arg(QStringLiteral("TotalMatches")), QStringLiteral("4"));
    const QString didl = root.arg(QStringLiteral("Result"));
    QStringList order;
    int pos = 0;
    while ((pos = didl.indexOf(QStringLiteral("<dc:title>"), pos)) != -1) {
        const int end = didl.indexOf(QStringLiteral("</dc:title>"), pos);
        order << didl.mid(pos + 10, end - pos - 10);
        pos = end;
    }
    QCOMPARE(order,
             (QStringList{QStringLiteral("Movies"), QStringLiteral("Shows"),
                         QStringLiteral("Books"), QStringLiteral("Audiobooks")}));
    QVERIFY(didl.contains(QStringLiteral("childCount=\"2\""))); // Movies
    QVERIFY(didl.contains(QStringLiteral("<upnp:class>object.container.storageFolder")));

    // Shows > show > season > episodes (ids resolved like a client: from titles).
    SoapReply shows = browseDirectChildren(m_fx.showsId);
    QVERIFY(shows.ok);
    const QString showId = containerIdForTitle(shows.arg(QStringLiteral("Result")),
                                                QStringLiteral("The Show"));
    QVERIFY(!showId.isEmpty());
    QCOMPARE(showId, m_fx.showCid);

    SoapReply seasons = browseDirectChildren(showId);
    QVERIFY(seasons.ok);
    const QString seasonId =
        containerIdForTitle(seasons.arg(QStringLiteral("Result")), QStringLiteral("Season 1"));
    QVERIFY(!seasonId.isEmpty());

    SoapReply eps = browseDirectChildren(seasonId);
    QVERIFY(eps.ok);
    QCOMPARE(eps.arg(QStringLiteral("NumberReturned")), QStringLiteral("2"));
    const QString epsDidl = eps.arg(QStringLiteral("Result"));
    QVERIFY(epsDidl.contains(QStringLiteral("<upnp:episodeNumber>1</upnp:episodeNumber>")));
    QVERIFY(epsDidl.contains(QStringLiteral("<upnp:episodeNumber>2</upnp:episodeNumber>")));
    QVERIFY(epsDidl.contains(QStringLiteral("<upnp:episodeSeason>1</upnp:episodeSeason>")));
    QVERIFY(epsDidl.contains(QStringLiteral("<upnp:seriesTitle>The Show</upnp:seriesTitle>")));
    QVERIFY(epsDidl.contains(QStringLiteral("http-get:*:video/x-matroska:")));

    // Metadata of the covered movie: duration/date/genre/albumArtURI.
    SoapReply meta = browseMetadata(m_fx.movie1Id);
    QVERIFY(meta.ok);
    const QString m1 = meta.arg(QStringLiteral("Result"));
    QVERIFY(m1.contains(QStringLiteral("duration=\"1:30:00.000\"")));
    QVERIFY(m1.contains(QStringLiteral("<dc:date>2024-01-01</dc:date>")));
    QVERIFY(m1.contains(QStringLiteral("<upnp:genre>Drama</upnp:genre>")));
    QVERIFY(m1.contains(QStringLiteral("dlna:profileID=\"JPEG_TN\"")));
    QVERIFY(m1.contains(QStringLiteral("/art/") + m_fx.movie1Id));

    // Caps + SystemUpdateID + ConnectionManager.
    const QString cd = QStringLiteral("urn:schemas-upnp-org:service:ContentDirectory:1");
    SoapReply searchCaps = soap(cd, QStringLiteral("GetSearchCapabilities"), {});
    QVERIFY(searchCaps.ok);
    QVERIFY(searchCaps.arg(QStringLiteral("SearchCaps")).isEmpty());
    SoapReply sortCaps =
        soap(cd, QStringLiteral("GetSortCapabilities"), {});
    QVERIFY(sortCaps.ok);
    QVERIFY(sortCaps.arg(QStringLiteral("SortCaps")).contains(QStringLiteral("dc:title")));
    SoapReply sysId = soap(cd, QStringLiteral("GetSystemUpdateID"), {});
    QVERIFY(sysId.ok);
    QVERIFY(sysId.arg(QStringLiteral("Id")).toInt() >= 1);

    const QString cm = QStringLiteral("urn:schemas-upnp-org:service:ConnectionManager:1");
    SoapReply proto = soap(cm, QStringLiteral("GetProtocolInfo"), {});
    QVERIFY(proto.ok);
    QVERIFY(proto.arg(QStringLiteral("Source")).contains(QStringLiteral("http-get:*:video/mp4:")));
    QVERIFY(proto.arg(QStringLiteral("Sink")).isEmpty());
    SoapReply connIds = soap(cm, QStringLiteral("GetCurrentConnectionIDs"), {});
    QVERIFY(connIds.ok);
    QCOMPARE(connIds.arg(QStringLiteral("ConnectionIDs")), QStringLiteral("0"));
    SoapReply connInfo = soap(cm, QStringLiteral("GetCurrentConnectionInfo"),
                              {{QStringLiteral("ConnectionID"), QStringLiteral("0")}});
    QVERIFY(connInfo.ok);
    QCOMPARE(connInfo.arg(QStringLiteral("Direction")), QStringLiteral("Output"));
    SoapReply connInfoBad = soap(cm, QStringLiteral("GetCurrentConnectionInfo"),
                                 {{QStringLiteral("ConnectionID"), QStringLiteral("5")}});
    QVERIFY(!connInfoBad.ok);
    QCOMPARE(connInfoBad.faultCode, 706);
}

void tst_dlna_server::browsePagingAndErrors()
{
    const QString cd = QStringLiteral("urn:schemas-upnp-org:service:ContentDirectory:1");
    SoapReply page = soap(cd, QStringLiteral("Browse"),
                          {{QStringLiteral("ObjectID"), QStringLiteral("t-movies")},
                           {QStringLiteral("BrowseFlag"), QStringLiteral("BrowseDirectChildren")},
                           {QStringLiteral("Filter"), QStringLiteral("*")},
                           {QStringLiteral("StartingIndex"), QStringLiteral("0")},
                           {QStringLiteral("RequestedCount"), QStringLiteral("1")},
                           {QStringLiteral("SortCriteria"), QString()}});
    QVERIFY(page.ok);
    QCOMPARE(page.arg(QStringLiteral("NumberReturned")), QStringLiteral("1"));
    QCOMPARE(page.arg(QStringLiteral("TotalMatches")), QStringLiteral("2"));
    QVERIFY(page.arg(QStringLiteral("Result")).contains(QStringLiteral("Alpha Movie")));

    SoapReply unknown = browseMetadata(QStringLiteral("i-nope"));
    QVERIFY(!unknown.ok);
    QCOMPARE(unknown.faultCode, 701);

    SoapReply badFlag = soap(cd, QStringLiteral("Browse"),
                             {{QStringLiteral("ObjectID"), QStringLiteral("0")},
                              {QStringLiteral("BrowseFlag"), QStringLiteral("BrowseEverything")},
                              {QStringLiteral("Filter"), QStringLiteral("*")},
                              {QStringLiteral("StartingIndex"), QStringLiteral("0")},
                              {QStringLiteral("RequestedCount"), QStringLiteral("0")},
                              {QStringLiteral("SortCriteria"), QString()}});
    QVERIFY(!badFlag.ok);
    QCOMPARE(badFlag.faultCode, 402);

    SoapReply badSort = soap(cd, QStringLiteral("Browse"),
                             {{QStringLiteral("ObjectID"), QStringLiteral("0")},
                              {QStringLiteral("BrowseFlag"),
                               QStringLiteral("BrowseDirectChildren")},
                              {QStringLiteral("Filter"), QStringLiteral("*")},
                              {QStringLiteral("StartingIndex"), QStringLiteral("0")},
                              {QStringLiteral("RequestedCount"), QStringLiteral("0")},
                              {QStringLiteral("SortCriteria"), QStringLiteral("title")}});
    QVERIFY(!badSort.ok);
    QCOMPARE(badSort.faultCode, 709);

    SoapReply badAction = soap(cd, QStringLiteral("Search"),
                               {{QStringLiteral("ContainerID"), QStringLiteral("0")}});
    QVERIFY(!badAction.ok);
    QCOMPARE(badAction.faultCode, 401);
}

// StartingIndex / RequestedCount are ui4 (0..2^32-1) straight off the wire. 2^31 and 2^32-1 used
// to be narrowed to a negative int (out-of-bounds index, bogus NumberReturned); every extreme
// must clamp cleanly, 2^32 is out of range, and the required arguments may not be absent.
void tst_dlna_server::browseHugeIndicesAndMissingArgs()
{
    const QString dc = QStringLiteral("BrowseDirectChildren");
    const QString movies = QStringLiteral("t-movies"); // 2 children
    for (const char* big : {"2147483647", "2147483648", "4294967295"}) {
        const QString v = QString::fromLatin1(big);
        SoapReply startFar = browseWith(movies, dc, v, QStringLiteral("0"));
        QVERIFY2(startFar.ok, big);
        QCOMPARE(startFar.arg(QStringLiteral("NumberReturned")), QStringLiteral("0"));
        QCOMPARE(startFar.arg(QStringLiteral("TotalMatches")), QStringLiteral("2"));
        SoapReply startFarCount = browseWith(movies, dc, v, v);
        QVERIFY2(startFarCount.ok, big);
        QCOMPARE(startFarCount.arg(QStringLiteral("NumberReturned")), QStringLiteral("0"));
        SoapReply countHuge = browseWith(movies, dc, QStringLiteral("0"), v);
        QVERIFY2(countHuge.ok, big);
        QCOMPARE(countHuge.arg(QStringLiteral("NumberReturned")), QStringLiteral("2"));
        QCOMPARE(countHuge.arg(QStringLiteral("TotalMatches")), QStringLiteral("2"));
        SoapReply tail = browseWith(movies, dc, QStringLiteral("1"), v);
        QVERIFY2(tail.ok, big);
        QCOMPARE(tail.arg(QStringLiteral("NumberReturned")), QStringLiteral("1"));
    }
    // One past ui4 is invalid, as is anything non-numeric or negative.
    for (const char* bad : {"4294967296", "-1", "abc", "1.5", "99999999999999999999"}) {
        const QString v = QString::fromLatin1(bad);
        SoapReply badStart = browseWith(movies, dc, v, QStringLiteral("0"));
        QVERIFY2(!badStart.ok, bad);
        QCOMPARE(badStart.faultCode, 402);
        SoapReply badCount = browseWith(movies, dc, QStringLiteral("0"), v);
        QVERIFY2(!badCount.ok, bad);
        QCOMPARE(badCount.faultCode, 402);
    }
    // Empty or missing required arguments -> 402 Invalid Args.
    SoapReply emptyStart = browseWith(movies, dc, QString(), QStringLiteral("0"));
    QVERIFY(!emptyStart.ok);
    QCOMPARE(emptyStart.faultCode, 402);
    SoapReply emptyCount = browseWith(movies, dc, QStringLiteral("0"), QString());
    QVERIFY(!emptyCount.ok);
    QCOMPARE(emptyCount.faultCode, 402);
    SoapReply emptyId = browseWith(QString(), dc, QStringLiteral("0"), QStringLiteral("0"));
    QVERIFY(!emptyId.ok);
    QCOMPARE(emptyId.faultCode, 402);
    SoapReply emptyFlag = browseWith(movies, QString(), QStringLiteral("0"), QStringLiteral("0"));
    QVERIFY(!emptyFlag.ok);
    QCOMPARE(emptyFlag.faultCode, 402);
    SoapReply missingAll = soap(QStringLiteral("urn:schemas-upnp-org:service:ContentDirectory:1"),
                                QStringLiteral("Browse"),
                                {{QStringLiteral("ObjectID"), movies}});
    QVERIFY(!missingAll.ok);
    QCOMPARE(missingAll.faultCode, 402);

    // The pure function clamps in 64 bits too (no narrowing before the clamp).
    const auto snap = m_provider.snapshot();
    DlnaContentDirectory::BrowseRequest br;
    br.objectId = movies;
    for (quint32 v : {quint32(0x7FFFFFFFu), quint32(0x80000000u), quint32(0xFFFFFFFFu)}) {
        br.startingIndex = v;
        br.requestedCount = 0xFFFFFFFFu;
        const auto r = DlnaContentDirectory::browse(*snap, m_baseUrl, br);
        QVERIFY(r.ok);
        QCOMPARE(r.numberReturned, quint32(0));
        QCOMPARE(r.totalMatches, quint32(2));
        br.startingIndex = 0;
        br.requestedCount = v;
        const auto r2 = DlnaContentDirectory::browse(*snap, m_baseUrl, br);
        QVERIFY(r2.ok);
        QCOMPARE(r2.numberReturned, quint32(2));
    }
}

// "-dc:title" flips the title order; any other criterion is accepted and ignored (it must not
// reverse the list), and containers stay ahead of items however the blocks are ordered.
void tst_dlna_server::browseSortOrder()
{
    auto titlesIn = [](const QString& didl) {
        QStringList out;
        int pos = 0;
        while ((pos = didl.indexOf(QStringLiteral("<dc:title>"), pos)) != -1) {
            const int end = didl.indexOf(QStringLiteral("</dc:title>"), pos);
            out << didl.mid(pos + 10, end - pos - 10);
            pos = end;
        }
        return out;
    };
    const QString dc = QStringLiteral("BrowseDirectChildren");
    const QString movies = QStringLiteral("t-movies");
    const QStringList asc{QStringLiteral("Alpha Movie"), QStringLiteral("Huge Movie")};
    const QStringList desc{QStringLiteral("Huge Movie"), QStringLiteral("Alpha Movie")};
    auto order = [&](const QString& sort) {
        SoapReply r = browseWith(movies, dc, QStringLiteral("0"), QStringLiteral("0"), sort);
        return r.ok ? titlesIn(r.arg(QStringLiteral("Result"))) : QStringList{QStringLiteral("!")};
    };
    QCOMPARE(order(QString()), asc);
    QCOMPARE(order(QStringLiteral("+dc:title")), asc);
    QCOMPARE(order(QStringLiteral("-dc:title")), desc);
    QCOMPARE(order(QStringLiteral("-dc:date")), asc);          // not title: no flip
    QCOMPARE(order(QStringLiteral("-dc:date,+dc:title")), asc);
    QCOMPARE(order(QStringLiteral("+dc:date,-dc:title")), desc);

    // Mixed block: two containers then two items; descending reverses each block, not the whole.
    DlnaSnapshot snap;
    auto addC = [&](const QString& id, const QString& parent, const QString& title) {
        DlnaContainer c;
        c.id = id;
        c.parentId = parent;
        c.title = title;
        c.upnpClass = QStringLiteral("object.container.storageFolder");
        snap.containers.insert(id, c);
        if (snap.containers.contains(parent))
            snap.containers[parent].childContainerIds.append(id);
    };
    addC(QStringLiteral("0"), QStringLiteral("-1"), QStringLiteral("Root"));
    addC(QStringLiteral("cA"), QStringLiteral("0"), QStringLiteral("Cont A"));
    addC(QStringLiteral("cB"), QStringLiteral("0"), QStringLiteral("Cont B"));
    for (const char* t : {"Item A", "Item B"}) {
        DlnaItem it = m_fx.item(QStringLiteral("i-%1").arg(QLatin1String(t).right(1)),
                                QStringLiteral("0"), QLatin1String(t),
                                QStringLiteral("object.item.videoItem"), m_fx.movie1,
                                QStringLiteral("video/mp4"));
        snap.items.insert(it.id, it);
        snap.containers[QStringLiteral("0")].childItemIds.append(it.id);
    }
    DlnaContentDirectory::BrowseRequest br;
    br.objectId = QStringLiteral("0");
    const auto up = DlnaContentDirectory::browse(snap, m_baseUrl, br);
    QCOMPARE(titlesIn(up.didl), (QStringList{QStringLiteral("Cont A"), QStringLiteral("Cont B"),
                                              QStringLiteral("Item A"), QStringLiteral("Item B")}));
    br.sortDescending = true;
    const auto down = DlnaContentDirectory::browse(snap, m_baseUrl, br);
    QCOMPARE(titlesIn(down.didl), (QStringList{QStringLiteral("Cont B"), QStringLiteral("Cont A"),
                                                QStringLiteral("Item B"), QStringLiteral("Item A")}));
}

// Titles are untrusted file names: XML-illegal characters (C0 controls, NUL, U+FFFE/FFFF, lone
// surrogates) must be dropped so the DIDL stays well-formed and a NUL cannot truncate it, the
// five markup characters must be escaped, and length is capped.
void tst_dlna_server::xmlHostileTitles()
{
    const auto original = m_provider.snapshot();
    auto snap = std::make_shared<DlnaSnapshot>(*original);
    QString hostile = QStringLiteral("Bad");
    hostile += QChar(0x01);
    hostile += QStringLiteral("Title");
    hostile += QChar(0x00);
    hostile += QStringLiteral("X");
    hostile += QChar(0x0B);
    hostile += QChar(0x0C);
    hostile += QStringLiteral(" & <b> \"q\" 'a' ");
    hostile += QChar(0xD800); // lone high surrogate
    hostile += QStringLiteral("mid");
    hostile += QChar(0xFFFE);
    hostile += QChar(0xFFFF);
    hostile += QChar(0xDC00); // lone low surrogate
    hostile += QString::fromUcs4(U"\U0001F3AC"); // a valid pair survives
    const QString expected = QStringLiteral("BadTitleX & <b> \"q\" 'a' mid")
                             + QString::fromUcs4(U"\U0001F3AC");
    snap->items[m_fx.movie1Id].title = hostile;
    snap->items[m_fx.bigId].title = QString(5000, QLatin1Char('x'));
    snap->containers[QStringLiteral("t-movies")].title = hostile;
    m_provider.publish(snap);

    auto parseTitles = [](const QString& didl, bool* wellFormed) {
        QXmlStreamReader xml(didl);
        QStringList titles;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.qualifiedName() == QLatin1String("dc:title"))
                titles << xml.readElementText();
        }
        *wellFormed = !xml.hasError();
        return titles;
    };
    SoapReply meta = browseMetadata(m_fx.movie1Id);
    QVERIFY(meta.ok);
    bool wellFormed = false;
    QStringList titles = parseTitles(meta.arg(QStringLiteral("Result")), &wellFormed);
    QVERIFY2(wellFormed, "DIDL must stay well-formed XML");
    QCOMPARE(titles, QStringList{expected});

    SoapReply kids = browseDirectChildren(QStringLiteral("t-movies"));
    QVERIFY(kids.ok);
    QCOMPARE(kids.arg(QStringLiteral("NumberReturned")), QStringLiteral("2"));
    titles = parseTitles(kids.arg(QStringLiteral("Result")), &wellFormed);
    QVERIFY(wellFormed);
    QCOMPARE(titles.size(), 2);
    QCOMPARE(titles.at(0), expected);
    QCOMPARE(titles.at(1).size(), 512); // capped

    SoapReply root = browseDirectChildren(QStringLiteral("0"));
    QVERIFY(root.ok);
    titles = parseTitles(root.arg(QStringLiteral("Result")), &wellFormed);
    QVERIFY(wellFormed);
    QVERIFY(titles.contains(expected)); // the hostile container title

    // The escape helper itself.
    QCOMPARE(DlnaContentDirectory::escape(hostile),
             QStringLiteral("BadTitleX &amp; &lt;b&gt; &quot;q&quot; &apos;a&apos; mid")
                 + QString::fromUcs4(U"\U0001F3AC"));

    m_provider.publish(original);
}

// ── media HTTP ───────────────────────────────────────────────────────────────────────────

void tst_dlna_server::mediaHttp()
{
    const QUrl url(m_baseUrl + "/m/" + m_fx.movie1Id + ".mp4");
    const Reply full =
        http(QStringLiteral("GET"), url,
             {{QByteArrayLiteral("getcontentFeatures.dlna.org"), QByteArrayLiteral("1")}});
    QCOMPARE(full.status, 200);
    QCOMPARE(full.headers.value("content-type"), QByteArray("video/mp4"));
    QVERIFY(full.headers.value("contentfeatures.dlna.org").contains(QByteArray("DLNA.ORG_OP=01")));
    QVERIFY(full.headers.value("contentfeatures.dlna.org").contains(QByteArray("DLNA.ORG_CI=0")));
    QVERIFY(full.headers.value("contentfeatures.dlna.org").contains(QByteArray("DLNA.ORG_FLAGS=0150")));
    QCOMPARE(full.headers.value("transfermode.dlna.org"), QByteArray("Streaming"));
    QCOMPARE(full.headers.value("accept-ranges"), QByteArray("bytes"));
    QCOMPARE(full.body.size(), 128 * 1024);

    const Reply head = http(QStringLiteral("HEAD"), url);
    QCOMPARE(head.status, 200);
    QCOMPARE(head.headers.value("content-length"), QByteArray::number(128 * 1024));
    QCOMPARE(head.body.size(), 0);

    // Mid-file Range, byte-exact against the fixture pattern (21 bytes at offset 1000).
    const Reply mid =
        http(QStringLiteral("GET"), url,
             {{QByteArrayLiteral("Range"), QByteArrayLiteral("bytes=1000-1020")}});
    QCOMPARE(mid.status, 206);
    QCOMPARE(mid.body, m_fx.midPattern);
    QCOMPARE(mid.headers.value("content-range"), QByteArray("bytes 1000-1020/131072"));

    // Cover art through /art with the thumbnail profile.
    const Reply art =
        http(QStringLiteral("GET"), QUrl(m_baseUrl + "/art/" + m_fx.movie1Id));
    QCOMPARE(art.status, 200);
    QCOMPARE(art.headers.value("content-type"), QByteArray("image/jpeg"));
    QVERIFY(art.headers.value("contentfeatures.dlna.org").contains(QByteArray("JPEG_TN")));
    QCOMPARE(art.headers.value("transfermode.dlna.org"), QByteArray("Interactive"));

    const Reply missing =
        http(QStringLiteral("GET"), QUrl(m_baseUrl + "/m/iiiiiiiiiiiiiiii.mp4"));
    QCOMPARE(missing.status, 404);
}

void tst_dlna_server::bigFileRanges()
{
    const QUrl url(m_baseUrl + "/m/" + m_fx.bigId + ".mkv");
    // HEAD proves the full 64-bit length (5 GiB) survives the pipe.
    const Reply head = http(QStringLiteral("HEAD"), url);
    QCOMPARE(head.status, 200);
    QCOMPARE(head.headers.value("content-length"), QByteArray::number(Fixture::kBigSize));

    // Range at 3,000,000,000: past 2^31.
    const Reply big =
        http(QStringLiteral("GET"), url,
             {{QByteArrayLiteral("Range"), QByteArrayLiteral("bytes=3000000000-3000000020")}});
    QCOMPARE(big.status, 206);
    QCOMPARE(big.body, m_fx.bigPattern);
    QCOMPARE(big.headers.value("content-range"),
             QByteArray("bytes 3000000000-3000000020/5368709120"));

    // Range at 4,500,000,000: past 2^32 - the real off_t truncation tripwire.
    const Reply huge =
        http(QStringLiteral("GET"), url,
             {{QByteArrayLiteral("Range"), QByteArrayLiteral("bytes=4500000000-4500000020")}});
    QCOMPARE(huge.status, 206);
    QCOMPARE(huge.body, m_fx.bigPattern2);
    QCOMPARE(huge.headers.value("content-range"),
             QByteArray("bytes 4500000000-4500000020/5368709120"));

    // Open-ended range near the tail: the last bytes of a >4 GiB file, exactly.
    const Reply tail =
        http(QStringLiteral("GET"), url,
             {{QByteArrayLiteral("Range"), QByteArrayLiteral("bytes=5368709110-")}});
    QCOMPARE(tail.status, 206);
    QCOMPARE(tail.body.size(), 10);
    QCOMPARE(tail.headers.value("content-range"),
             QByteArray("bytes 5368709110-5368709119/5368709120"));
}

void tst_dlna_server::epubDownload()
{
    const Reply epub =
        http(QStringLiteral("GET"), QUrl(m_baseUrl + "/m/" + m_fx.bookId + ".epub"));
    QCOMPARE(epub.status, 200);
    QCOMPARE(epub.headers.value("content-type"), QByteArray("application/epub+zip"));
    QCOMPARE(epub.headers.value("transfermode.dlna.org"), QByteArray("Background"));
    QVERIFY(epub.body.startsWith("PK"));
    QVERIFY(epub.headers.value("contentfeatures.dlna.org").contains(QByteArray("DLNA.ORG_OP=01")));

    SoapReply meta = browseMetadata(m_fx.bookId);
    QVERIFY(meta.ok);
    const QString didl = meta.arg(QStringLiteral("Result"));
    QVERIFY(didl.contains(QStringLiteral("<upnp:class>object.item.textItem</upnp:class>")));
    QVERIFY(didl.contains(QStringLiteral("http-get:*:application/epub+zip:")));
    QVERIFY(didl.contains(QStringLiteral("<dc:creator>Author Name</dc:creator>")));
}

void tst_dlna_server::subtitleServing()
{
    // ep2 has a sibling .srt: the DIDL carries the extra res + Samsung CaptionInfoEx...
    SoapReply meta = browseMetadata(m_fx.ep2Id);
    QVERIFY(meta.ok);
    const QString didl = meta.arg(QStringLiteral("Result"));
    QVERIFY(didl.contains(QStringLiteral("http-get:*:text/srt:*")));
    QVERIFY(didl.contains(QStringLiteral("<sec:CaptionInfoEx sec:type=\"srt\">")));
    QVERIFY(didl.contains(QStringLiteral("/sub/") + m_fx.ep2Id + QStringLiteral(".srt")));

    // Samsung TVs ask for the subtitle URL with getCaptionInfo.sec on the media request itself.
    const QUrl ep2Url(m_baseUrl + "/m/" + m_fx.ep2Id + ".mkv");
    const Reply captioned =
        http(QStringLiteral("HEAD"), ep2Url,
             {{QByteArrayLiteral("getCaptionInfo.sec"), QByteArrayLiteral("1")}});
    QCOMPARE(captioned.status, 200);
    QCOMPARE(captioned.headers.value("captioninfo.sec"),
             (m_baseUrl + "/sub/" + m_fx.ep2Id + ".srt").toLatin1());
    const Reply plain = http(QStringLiteral("HEAD"), ep2Url);
    QCOMPARE(plain.status, 200);
    QVERIFY(!plain.headers.contains("captioninfo.sec"));
    const Reply noSub =
        http(QStringLiteral("HEAD"), QUrl(m_baseUrl + "/m/" + m_fx.ep1Id + ".mkv"),
             {{QByteArrayLiteral("getCaptionInfo.sec"), QByteArrayLiteral("1")}});
    QCOMPARE(noSub.status, 200);
    QVERIFY(!noSub.headers.contains("captioninfo.sec"));

    // ...and /sub serves the bytes.
    const Reply sub =
        http(QStringLiteral("GET"), QUrl(m_baseUrl + "/sub/" + m_fx.ep2Id + ".srt"));
    QCOMPARE(sub.status, 200);
    QCOMPARE(sub.headers.value("content-type"), QByteArray("text/srt"));
    QVERIFY(sub.body.contains("hello"));

    // ep1 has no sibling: no subtitle elements.
    SoapReply meta1 = browseMetadata(m_fx.ep1Id);
    QVERIFY(meta1.ok);
    QVERIFY(!meta1.arg(QStringLiteral("Result")).contains(QStringLiteral("CaptionInfoEx")));
}

// A file that no longer matches the snapshot's size+mtime must be refused BEFORE the headers go
// out (404), never answered with a 200 whose body stops short of the promised Content-Length.
void tst_dlna_server::changedFileIsNotServedTruncated()
{
    const QString path = m_tmp.path() + "/volatile.mp4";
    QVERIFY(!writePatterned(path, 64 * 1024, 0, {}).isEmpty());
    const QString id = QStringLiteral("i00000000000000a1");
    const auto original = m_provider.snapshot();
    auto publishFresh = [&] {
        auto snap = std::make_shared<DlnaSnapshot>(*original);
        snap->items.insert(id, m_fx.item(id, QStringLiteral("t-movies"),
                                         QStringLiteral("Volatile"),
                                         QStringLiteral("object.item.videoItem.movie"), path,
                                         QStringLiteral("video/mp4")));
        snap->containers[QStringLiteral("t-movies")].childItemIds.append(id);
        snap->itemCount = snap->items.size();
        m_provider.publish(snap);
    };
    const QUrl url(m_baseUrl + "/m/" + id + ".mp4");
    publishFresh();
    const Reply ok = http(QStringLiteral("GET"), url);
    QCOMPARE(ok.status, 200);
    QCOMPARE(ok.body.size(), 64 * 1024);

    // Grows after the snapshot: GET and HEAD both 404, no short 200.
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::Append));
        f.write(QByteArray(1000, 'z'));
    }
    const Reply grownGet = http(QStringLiteral("GET"), url);
    QCOMPARE(grownGet.status, 404);
    QCOMPARE(grownGet.body.size() < 64 * 1024, true);
    QCOMPARE(http(QStringLiteral("HEAD"), url).status, 404);

    // Same size, different mtime: also not the bytes the snapshot described.
    publishFresh();
    QCOMPARE(http(QStringLiteral("GET"), url).body.size(), 64 * 1024 + 1000);
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::currentDateTime().addSecs(120),
                              QFileDevice::FileModificationTime));
    }
    QCOMPARE(http(QStringLiteral("GET"), url).status, 404);

    // A refreshed snapshot serves the new length, in full.
    publishFresh();
    const Reply refreshed = http(QStringLiteral("GET"), url);
    QCOMPARE(refreshed.status, 200);
    QCOMPARE(refreshed.body.size(), 64 * 1024 + 1000);
    QCOMPARE(refreshed.headers.value("content-length"), QByteArray::number(64 * 1024 + 1000));
    m_provider.publish(original);
}

// /art serves jpg/jpeg/png thumbnails up to 10 MB and nothing else; the DIDL never advertises
// a cover the server would refuse.
void tst_dlna_server::coverArtRules()
{
    const auto original = m_provider.snapshot();
    auto withCover = [&](const QString& coverPath) {
        auto snap = std::make_shared<DlnaSnapshot>(*original);
        snap->items[m_fx.movie1Id].coverPath = coverPath;
        m_provider.publish(snap);
    };
    auto writeBytes = [&](const QString& name, qint64 size) {
        QFile f(m_tmp.path() + "/" + name);
        if (!f.open(QIODevice::WriteOnly))
            return QString();
        f.write(QByteArray(int(size), 'c'));
        return QFileInfo(f).absoluteFilePath();
    };
    const QUrl art(m_baseUrl + "/art/" + m_fx.movie1Id);

    withCover(writeBytes(QStringLiteral("cover.png"), 2048));
    const Reply png = http(QStringLiteral("GET"), art);
    QCOMPARE(png.status, 200);
    QCOMPARE(png.headers.value("content-type"), QByteArray("image/png"));
    QVERIFY(browseMetadata(m_fx.movie1Id).arg(QStringLiteral("Result"))
                .contains(QStringLiteral("dlna:profileID=\"PNG_TN\"")));

    withCover(writeBytes(QStringLiteral("cover.webp"), 2048));
    QCOMPARE(http(QStringLiteral("GET"), art).status, 404);
    QVERIFY(!browseMetadata(m_fx.movie1Id).arg(QStringLiteral("Result"))
                 .contains(QStringLiteral("albumArtURI")));

    withCover(writeBytes(QStringLiteral("cover.exe.jpg.txt"), 16));
    QCOMPARE(http(QStringLiteral("GET"), art).status, 404);

    withCover(writeBytes(QStringLiteral("huge.jpg"), 10 * 1024 * 1024 + 1));
    QCOMPARE(http(QStringLiteral("GET"), art).status, 404); // over the 10 MB cap
    withCover(writeBytes(QStringLiteral("limit.jpg"), 10 * 1024 * 1024));
    QCOMPARE(http(QStringLiteral("GET"), art).status, 200); // exactly at the cap

    withCover(m_tmp.path()); // a directory is not a cover
    QCOMPARE(http(QStringLiteral("GET"), art).status, 404);
    m_provider.publish(original);
}

// ── GENA ─────────────────────────────────────────────────────────────────────────────────

void tst_dlna_server::genaEvents()
{
    NotifyListener listener;
    QVERIFY(listener.start());

    QNetworkRequest req(QUrl(m_baseUrl + "/event/cd"));
    req.setRawHeader("NT", "upnp:event");
    req.setRawHeader("TIMEOUT", "Second-1800");
    req.setRawHeader(
        "CALLBACK",
        QStringLiteral("<http://127.0.0.1:%1/cb>").arg(listener.port()).toLatin1());
    QNetworkReply* sub = m_nam.sendCustomRequest(req, "SUBSCRIBE");
    QEventLoop loop1;
    connect(sub, &QNetworkReply::finished, &loop1, &QEventLoop::quit);
    QTimer::singleShot(10000, &loop1, &QEventLoop::quit);
    loop1.exec();
    QCOMPARE(int(sub->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()), 200);
    const QByteArray sid = sub->rawHeader("SID");
    QVERIFY(sid.startsWith("uuid:"));
    sub->deleteLater();

    // Initial NOTIFY (SEQ 0) with the current SystemUpdateID.
    QTRY_COMPARE_WITH_TIMEOUT(listener.events.size(), 1, 10000);
    QVERIFY(listener.events.first().contains(QStringLiteral("<SystemUpdateID>1</SystemUpdateID>")));

    // Publish snapshot 2 and tell the server: a NOTIFY with the incremented id follows
    // (rate-limited to >= 2 s after the initial accept, so allow up to 10 s).
    auto snap2 = std::make_shared<DlnaSnapshot>(*m_provider.snapshot());
    snap2->systemUpdateId = 2;
    m_provider.publish(snap2);
    m_server->handleSnapshotChanged();
    QTRY_COMPARE_WITH_TIMEOUT(listener.events.size(), 2, 10000);
    const QString second = listener.events.at(1);
    QVERIFY(second.contains(QStringLiteral("<SystemUpdateID>2</SystemUpdateID>")));
    QVERIFY(second.contains(QStringLiteral("<ContainerUpdateIDs>0,")));
}

// ── revocation, stop, restart ────────────────────────────────────────────────────────────

void tst_dlna_server::revocation()
{
    // Plain GET on the 5 GiB file via a raw socket; once bytes flow, remove the item from
    // the CURRENT snapshot: the server must abort mid-body (connection ends early).
    QTcpSocket sock;
    sock.connectToHost(QStringLiteral("127.0.0.1"), m_boundPort);
    QVERIFY(sock.waitForConnected(5000));
    sock.write(QStringLiteral("GET /m/%1.mkv HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
                   .arg(m_fx.bigId)
                   .toLatin1());
    QVERIFY(sock.waitForBytesWritten(5000));

    qint64 received = 0;
    bool headersSeen = false;
    while (sock.state() == QAbstractSocket::ConnectedState && received < 4 * 1024 * 1024) {
        if (!sock.waitForReadyRead(3000))
            break;
        const QByteArray chunk = sock.readAll();
        if (chunk.isEmpty())
            continue;
        received += chunk.size();
        if (!headersSeen) {
            headersSeen = true;
            QVERIFY(chunk.contains("200"));
            QVERIFY(chunk.contains("5368709120"));
        }
    }
    QVERIFY2(received > 0, "server started streaming the big file");
    QVERIFY2(received < 1024 * 1024 * 1024, "not already the whole file");

    auto snap3 = std::make_shared<DlnaSnapshot>(*m_provider.snapshot());
    snap3->items.remove(m_fx.bigId);
    snap3->containers[QStringLiteral("t-movies")].childItemIds.removeAll(m_fx.bigId);
    snap3->itemCount = snap3->items.size();
    m_provider.publish(snap3);

    QElapsedTimer deadline;
    deadline.start();
    while (sock.state() == QAbstractSocket::ConnectedState && deadline.elapsed() < 30000) {
        if (sock.waitForReadyRead(1000))
            received += sock.readAll().size();
    }
    QVERIFY2(sock.state() != QAbstractSocket::ConnectedState,
             "connection ended early after revocation");
    QVERIFY(received < 1024 * 1024 * 1024); // nowhere near the full 5 GiB
    sock.close();
}

void tst_dlna_server::stopClosesPort()
{
    m_server->setEnabled(false);
    QTRY_COMPARE_WITH_TIMEOUT(m_server->running(), false, 15000);
    QTRY_COMPARE_WITH_TIMEOUT(m_server->busy(), false, 15000);
    QCOMPARE(m_settings->value(QStringLiteral("dlnaSharing/enabled")).toBool(), false);

    QTcpSocket probe;
    probe.connectToHost(QStringLiteral("127.0.0.1"), m_boundPort);
    QVERIFY(!probe.waitForConnected(2000));
    probe.close();
}

void tst_dlna_server::restartReusesUdnAndPort()
{
    m_settings->sync();
    DlnaServer second(this);
    second.setSettings(m_settings);
    second.setSnapshotProvider([this] { return m_provider.snapshot(); });
    const auto loopback = DlnaNetwork::loopbackAdapter();
    QVERIFY(loopback.has_value());
    second.setForcedInterface(*loopback);
    second.setEnabled(true);
    QTRY_COMPARE_WITH_TIMEOUT(second.running(), true, 15000);
    QCOMPARE(quint16(m_settings->value(QStringLiteral("dlnaSharing/port")).toUInt()),
             m_boundPort); // saved port reused so TVs' cached locations stay valid
    QCOMPARE(m_settings->value(QStringLiteral("dlnaSharing/uuid")).toString(), m_udn);

    // And the restarted server still browses.
    SoapReply root = soap(QStringLiteral("urn:schemas-upnp-org:service:ContentDirectory:1"),
                          QStringLiteral("Browse"),
                          {{QStringLiteral("ObjectID"), QStringLiteral("0")},
                           {QStringLiteral("BrowseFlag"), QStringLiteral("BrowseDirectChildren")},
                           {QStringLiteral("Filter"), QStringLiteral("*")},
                           {QStringLiteral("StartingIndex"), QStringLiteral("0")},
                           {QStringLiteral("RequestedCount"), QStringLiteral("0")},
                           {QStringLiteral("SortCriteria"), QString()}});
    QVERIFY(root.ok);
    second.setEnabled(false);
    QTRY_COMPARE_WITH_TIMEOUT(second.running(), false, 15000);
    second.stopForQuit();
}

// Sharing is a standing wish: no network at start, a network that drops, and a changed address
// must all heal on the poll timer alone. The loopback adapter plays "the home network" through
// the home-adapter provider seam; the second fake adapter has the same name but another IP.
void tst_dlna_server::resumesAfterNetworkLoss()
{
    const auto loopback = DlnaNetwork::loopbackAdapter();
    QVERIFY(loopback.has_value());
    QSettings settings(m_tmp.path() + "/dlna-resume.ini", QSettings::IniFormat);
    DlnaServer srv;
    srv.setSettings(&settings);
    srv.setSnapshotProvider([this] { return m_provider.snapshot(); });
    enum Net { Absent, Present, ChangedIp } net = Absent;
    srv.setHomeAdapterProvider([&]() -> std::optional<DlnaNetAdapter> {
        if (net == Absent)
            return std::nullopt;
        DlnaNetAdapter a = *loopback;
        if (net == ChangedIp)
            a.ip = QStringLiteral("10.9.9.9");
        return a;
    });
    srv.setPollIntervalMs(200);

    // 1) enabled with no network (boot without Wi-Fi): waits, says why, does not give up.
    srv.setEnabled(true);
    QVERIFY(srv.enabled());
    QVERIFY(!srv.running());
    QVERIFY(!srv.busy());
    QCOMPARE(srv.error(), QStringLiteral("No home network found"));
    QTest::qWait(700); // several polls with nothing to start
    QVERIFY(!srv.running());
    QCOMPARE(srv.error(), QStringLiteral("No home network found"));

    // 2) the network shows up: sharing starts by itself and the error clears.
    net = Present;
    QTRY_COMPARE_WITH_TIMEOUT(srv.running(), true, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!srv.busy(), 5000);
    QVERIFY2(srv.error().isEmpty(), qPrintable(srv.error()));
    QVERIFY(srv.networkLabel().contains(QStringLiteral("127.0.0.1")));

    // 3) the network drops (Wi-Fi off, sleep): stops cleanly, intent (enabled) survives.
    net = Absent;
    QTRY_COMPARE_WITH_TIMEOUT(srv.running(), false, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!srv.busy(), 5000);
    QVERIFY(srv.enabled());
    QCOMPARE(srv.error(), QStringLiteral("No home network found"));

    // 4) ...and comes back: sharing resumes.
    net = Present;
    QTRY_COMPARE_WITH_TIMEOUT(srv.running(), true, 15000);
    QVERIFY2(srv.error().isEmpty(), qPrintable(srv.error()));

    // 5) the same adapter takes another address (DHCP renewal): detected, byebye, re-bind
    //    attempt. Here the fake address is not what pupnp actually binds, so the bound-address
    //    verification refuses to advertise it - which also proves both guards.
    net = ChangedIp;
    QTRY_COMPARE_WITH_TIMEOUT(srv.running(), false, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(srv.error().contains(QStringLiteral("not a home network address")),
                             15000);
    net = Present;
    QTRY_COMPARE_WITH_TIMEOUT(srv.running(), true, 15000);
    QVERIFY2(srv.error().isEmpty(), qPrintable(srv.error()));

    // 6) switching off is final: no more polling restarts, error gone.
    srv.setEnabled(false);
    QTRY_COMPARE_WITH_TIMEOUT(srv.running(), false, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!srv.busy(), 5000);
    QVERIFY(srv.error().isEmpty());
    QTest::qWait(700);
    QVERIFY(!srv.running());
    srv.stopForQuit();
}

namespace {
bool openRawGet(QTcpSocket& sock, quint16 port, const QString& id)
{
    sock.setReadBufferSize(256 * 1024); // read only what the test pulls: the OS buffers must fill
    sock.connectToHost(QStringLiteral("127.0.0.1"), port);
    if (!sock.waitForConnected(5000))
        return false;
    sock.write(QStringLiteral("GET /m/%1.mkv HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
                   .arg(id)
                   .toLatin1());
    return sock.waitForBytesWritten(5000);
}
} // namespace

// A client that opens a GET and stops reading must not pin a worker forever (pupnp patch 0002:
// every body slice has an idle budget, here 3 s), while a client that keeps reading - however
// slowly - is never cut off by it.
void tst_dlna_server::stalledClientFreesWorker()
{
    m_provider.publish(m_fx.snapshot1()); // the revocation test removed the big file
    const auto loopback = DlnaNetwork::loopbackAdapter();
    QVERIFY(loopback.has_value());
    QSettings settings(m_tmp.path() + "/dlna-stall.ini", QSettings::IniFormat);
    DlnaServer srv;
    srv.setSettings(&settings);
    srv.setSnapshotProvider([this] { return m_provider.snapshot(); });
    srv.setForcedInterface(*loopback);
    srv.setWriteIdleTimeoutSeconds(3);
    srv.setEnabled(true);
    QTRY_COMPARE_WITH_TIMEOUT(srv.running(), true, 15000);
    const quint16 port = quint16(settings.value(QStringLiteral("dlnaSharing/port")).toUInt());
    QVERIFY(port != 0);
    QTRY_COMPARE_WITH_TIMEOUT(DlnaMediaFiles::activeHandles(), 0, 5000);

    QTcpSocket sock;
    QVERIFY(openRawGet(sock, port, m_fx.bigId));
    QTRY_COMPARE_WITH_TIMEOUT(DlnaMediaFiles::activeHandles(), 1, 5000);

    // Slow but moving: ~2 MB/s for 7 s, i.e. well past the 3 s idle budget, is never cut off.
    QElapsedTimer moving;
    moving.start();
    qint64 got = 0;
    while (moving.elapsed() < 7000) {
        if (sock.bytesAvailable() == 0)
            sock.waitForReadyRead(200);
        got += sock.read(64 * 1024).size();
        QTest::qWait(30);
    }
    QVERIFY2(got > 1024 * 1024, "the slow reader kept receiving");
    QCOMPARE(sock.state(), QAbstractSocket::ConnectedState);
    QCOMPARE(DlnaMediaFiles::activeHandles(), 1);

    // Now stop reading entirely: the worker must free itself within idle budget + slack.
    QElapsedTimer stalled;
    stalled.start();
    QTRY_COMPARE_WITH_TIMEOUT(DlnaMediaFiles::activeHandles(), 0, 25000);
    QVERIFY2(stalled.elapsed() >= 1500, "freed by the idle timeout, not by something instant");
    qInfo() << "stalled worker freed after" << stalled.elapsed() << "ms (budget 3000)";
    sock.abort();

    // The server is healthy afterwards.
    QTcpSocket probe;
    probe.connectToHost(QStringLiteral("127.0.0.1"), port);
    QVERIFY(probe.waitForConnected(5000));
    probe.write(QStringLiteral("GET /m/%1.mp4 HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
                    .arg(m_fx.movie1Id)
                    .toLatin1());
    QByteArray all;
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 10000 && probe.state() == QAbstractSocket::ConnectedState) {
        probe.waitForReadyRead(500);
        all += probe.readAll();
    }
    all += probe.readAll();
    QVERIFY(all.startsWith("HTTP/1.1 200"));
    QVERIFY(all.size() > 128 * 1024);

    srv.setEnabled(false);
    QTRY_COMPARE_WITH_TIMEOUT(srv.running(), false, 15000);
    srv.stopForQuit();
}

void tst_dlna_server::cleanupTestCase()
{
    if (m_server)
        m_server->stopForQuit();
}

QTEST_MAIN(tst_dlna_server)
#include "tst_dlna_server.moc"
