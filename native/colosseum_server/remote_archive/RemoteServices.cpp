#include "RemoteServices.h"
#include "LzString.h"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>
#include <QXmlStreamReader>

#include <algorithm>

namespace Colosseum::Server::RemoteArchive {
namespace {

constexpr auto kDlnaTransferMode = "Streaming";
constexpr auto kDlnaFeatures = "DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000";

QString sha256Hex(const QString &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}

struct FtpReply {
    int code = 0;
    QStringList lines;
    [[nodiscard]] bool positive() const { return code >= 200 && code < 400; }
};

class FtpConnection {
public:
    bool open(const QUrl &url, RemoteError *error)
    {
        m_url = url;
        const QString scheme = url.scheme().toLower();
        if (scheme != QStringLiteral("ftp") && scheme != QStringLiteral("ftps")) {
            if (error) *error = {RemoteErrorCode::InvalidRequest, QStringLiteral("unknown FTP protocol"), 0};
            return false;
        }
        m_tls = scheme == QStringLiteral("ftps");
        m_socket = std::make_unique<QSslSocket>();
        QObject::connect(m_socket.get(), &QSslSocket::sslErrors, m_socket.get(),
                         [this](const QList<QSslError> &) { m_socket->ignoreSslErrors(); });
        m_socket->connectToHost(url.host(), url.port(21));
        if (!m_socket->waitForConnected(10000)) {
            if (error) *error = {RemoteErrorCode::Transport, m_socket->errorString(), 0};
            return false;
        }
        FtpReply greeting = readReply(error);
        if (greeting.code != 220) return failProtocol(QStringLiteral("FTP greeting rejected"), greeting, error);
        if (m_tls) {
            FtpReply auth = command(QStringLiteral("AUTH TLS"), error);
            if (auth.code != 234 && auth.code != 334) return failProtocol(QStringLiteral("AUTH TLS rejected"), auth, error);
            m_socket->startClientEncryption();
            if (!m_socket->waitForEncrypted(10000)) {
                if (error) *error = {RemoteErrorCode::Transport, m_socket->errorString(), 0};
                return false;
            }
        }
        const QString user = url.userName(QUrl::FullyDecoded).isEmpty()
            ? QStringLiteral("anonymous") : url.userName(QUrl::FullyDecoded);
        const QString pass = url.password(QUrl::FullyDecoded).isEmpty()
            ? QStringLiteral("guest") : url.password(QUrl::FullyDecoded);
        FtpReply userReply = command(QStringLiteral("USER %1").arg(user), error);
        if (userReply.code == 331) {
            FtpReply passReply = command(QStringLiteral("PASS %1").arg(pass), error);
            if (!passReply.positive()) return failProtocol(QStringLiteral("FTP password rejected"), passReply, error);
        } else if (!userReply.positive()) {
            return failProtocol(QStringLiteral("FTP user rejected"), userReply, error);
        }
        if (m_tls) {
            command(QStringLiteral("PBSZ 0"), nullptr);
            FtpReply prot = command(QStringLiteral("PROT P"), error);
            if (!prot.positive()) return failProtocol(QStringLiteral("FTP data protection rejected"), prot, error);
        }
        FtpReply type = command(QStringLiteral("TYPE I"), error);
        if (!type.positive()) return failProtocol(QStringLiteral("FTP binary mode rejected"), type, error);
        if (error) *error = {};
        return true;
    }

    FtpReply command(const QString &commandText, RemoteError *error)
    {
        if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
            if (error) *error = {RemoteErrorCode::Transport, QStringLiteral("FTP control socket is not connected"), 0};
            return {};
        }
        const QByteArray wire = commandText.toUtf8() + "\r\n";
        if (m_socket->write(wire) != wire.size() || !m_socket->waitForBytesWritten(5000)) {
            if (error) *error = {RemoteErrorCode::Transport, m_socket->errorString(), 0};
            return {};
        }
        return readReply(error);
    }

    QByteArray retrieve(const QString &path, qint64 start, CancellationToken *cancel, RemoteError *error)
    {
        quint16 dataPort = 0;
        FtpReply epsv = command(QStringLiteral("EPSV"), nullptr);
        if (epsv.code == 229 && !epsv.lines.isEmpty()) {
            static const QRegularExpression re(QStringLiteral(R"(\(\|\|\|(\d+)\|\))"));
            const auto match = re.match(epsv.lines.last());
            if (match.hasMatch()) dataPort = static_cast<quint16>(match.captured(1).toUShort());
        }
        if (!dataPort) {
            FtpReply pasv = command(QStringLiteral("PASV"), error);
            if (pasv.code != 227 || pasv.lines.isEmpty()) return {};
            static const QRegularExpression re(QStringLiteral(R"(\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\))"));
            const auto match = re.match(pasv.lines.last());
            if (!match.hasMatch()) {
                if (error) *error = {RemoteErrorCode::Protocol, QStringLiteral("invalid PASV response"), pasv.code};
                return {};
            }
            dataPort = static_cast<quint16>(match.captured(5).toInt() * 256 + match.captured(6).toInt());
        }

        auto data = std::make_unique<QSslSocket>();
        QObject::connect(data.get(), &QSslSocket::sslErrors, data.get(),
                         [ptr = data.get()](const QList<QSslError> &) { ptr->ignoreSslErrors(); });
        if (m_tls) {
            data->connectToHostEncrypted(m_url.host(), dataPort);
            if (!data->waitForEncrypted(10000)) {
                if (error) *error = {RemoteErrorCode::Transport, data->errorString(), 0};
                return {};
            }
        } else {
            data->connectToHost(m_url.host(), dataPort);
            if (!data->waitForConnected(10000)) {
                if (error) *error = {RemoteErrorCode::Transport, data->errorString(), 0};
                return {};
            }
        }
        if (start > 0) {
            FtpReply rest = command(QStringLiteral("REST %1").arg(start), error);
            if (rest.code != 350) return {};
        }
        if (!m_socket->write(QStringLiteral("RETR %1\r\n").arg(path).toUtf8()) || !m_socket->waitForBytesWritten(5000)) {
            if (error) *error = {RemoteErrorCode::Transport, m_socket->errorString(), 0};
            return {};
        }
        FtpReply preliminary = readReply(error);
        if (preliminary.code != 125 && preliminary.code != 150) {
            failProtocol(QStringLiteral("FTP RETR rejected"), preliminary, error);
            return {};
        }
        QByteArray bytes;
        while (data->state() != QAbstractSocket::UnconnectedState || data->bytesAvailable() > 0) {
            if (cancel && cancel->isCancelled()) {
                data->abort();
                if (error) *error = {RemoteErrorCode::Cancelled, QStringLiteral("cancelled"), 0};
                return {};
            }
            if (data->bytesAvailable() > 0) bytes += data->readAll();
            else if (!data->waitForReadyRead(100) && data->state() == QAbstractSocket::UnconnectedState) break;
        }
        bytes += data->readAll();
        FtpReply complete = readReply(error);
        if (complete.code != 226 && complete.code != 250) return {};
        if (error) *error = {};
        return bytes;
    }

    void close()
    {
        if (!m_socket) return;
        if (m_socket->state() == QAbstractSocket::ConnectedState) command(QStringLiteral("QUIT"), nullptr);
        m_socket->disconnectFromHost();
    }

private:
    FtpReply readReply(RemoteError *error)
    {
        FtpReply reply;
        QByteArray firstCode;
        bool multiline = false;
        while (true) {
            QByteArray line;
            while (!line.endsWith('\n')) {
                if (m_socket->canReadLine()) { line += m_socket->readLine(); break; }
                if (!m_socket->waitForReadyRead(10000)) {
                    if (error) *error = {RemoteErrorCode::Transport, m_socket->errorString(), 0};
                    return {};
                }
            }
            line = line.trimmed();
            if (line.size() < 3) continue;
            if (firstCode.isEmpty()) {
                firstCode = line.left(3);
                bool ok = false; reply.code = firstCode.toInt(&ok);
                if (!ok) {
                    if (error) *error = {RemoteErrorCode::Protocol, QStringLiteral("invalid FTP reply"), 0};
                    return {};
                }
                multiline = line.size() > 3 && line[3] == '-';
            }
            reply.lines.push_back(QString::fromUtf8(line));
            if (!multiline || (line.startsWith(firstCode) && line.size() > 3 && line[3] == ' ')) break;
        }
        if (error) *error = {};
        return reply;
    }

    bool failProtocol(const QString &message, const FtpReply &reply, RemoteError *error)
    {
        if (error) *error = {RemoteErrorCode::Protocol,
                             message + (reply.lines.isEmpty() ? QString() : QStringLiteral(": ") + reply.lines.last()),
                             reply.code};
        return false;
    }

    QUrl m_url;
    bool m_tls = false;
    std::unique_ptr<QSslSocket> m_socket;
};

QDateTime parseFtpTimestamp(QString value)
{
    value = value.trimmed();
    if (value.size() >= 14) {
        const QDateTime parsed = QDateTime::fromString(value.left(14), QStringLiteral("yyyyMMddHHmmss"));
        if (parsed.isValid()) return QDateTime(parsed.date(), parsed.time(), QTimeZone::UTC);
    }
    return {};
}

struct SimpleRange {
    bool present = false;
    bool valid = true;
    qint64 start = 0;
    qint64 end = -1;
};

SimpleRange parseRange(const QByteArray &raw, qint64 size)
{
    SimpleRange range;
    if (raw.isEmpty()) return range;
    range.present = true;
    if (!raw.startsWith("bytes=")) { range.valid = false; return range; }
    const QByteArray value = raw.mid(6);
    const int dash = value.indexOf('-');
    if (dash < 0) { range.valid = false; return range; }
    const QByteArray left = value.left(dash);
    QByteArray right = value.mid(dash + 1);
    const int slash = right.indexOf('/');
    if (slash >= 0) right.truncate(slash);
    bool okLeft = false, okRight = false;
    if (!left.isEmpty()) range.start = left.toLongLong(&okLeft);
    if (!right.isEmpty()) range.end = right.toLongLong(&okRight);
    if (left.isEmpty() && !right.isEmpty()) {
        if (!okRight || range.end <= 0) { range.valid = false; return range; }
        const qint64 suffix = range.end;
        range.start = std::max<qint64>(0, size - suffix); range.end = size - 1;
    } else if (!left.isEmpty()) {
        if (!okLeft) { range.valid = false; return range; }
        if (right.isEmpty()) range.end = size - 1;
        else if (!okRight) range.valid = false;
    } else range.valid = false;
    if (range.start < 0 || range.start >= size || range.end < range.start || range.end >= size) range.valid = false;
    return range;
}

QString extractFileName(const QString &subject, const QString &extensions)
{
    QRegularExpression quoted(QStringLiteral("\\\"([^\\\"]+\\.(?:%1))\\\"").arg(extensions),
                              QRegularExpression::CaseInsensitiveOption);
    auto match = quoted.match(subject);
    if (match.hasMatch()) return match.captured(1);
    QRegularExpression plain(QStringLiteral(R"(([^\s"']+\.(?:%1)))").arg(extensions),
                             QRegularExpression::CaseInsensitiveOption);
    match = plain.match(subject);
    return match.hasMatch() ? match.captured(1) : QString();
}

bool archiveFileKind(const QString &subject, ArchiveKind *kind)
{
    const QString upper = subject.toUpper();
    static const QRegularExpression partRar(QStringLiteral(R"(\.PART\d{1,5}\.RAR(?:"|$))"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rxx(QStringLiteral(R"(\.(?:R\d{1,5}|RAR)(?:"|$))"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression seven(QStringLiteral(R"(\.7Z(?:\.\d{1,5})?(?:"|$))"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression zip(QStringLiteral(R"(\.(?:Z\d{1,5}|ZIP)(?:"|$))"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tar(QStringLiteral(R"(\.TAR(?:\.\d{1,5})?(?:"|$))"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tgz(QStringLiteral(R"(\.(?:TAR\.GZ(?:\.\d{1,5})?|TGZ(?:\.\d{1,5})?|GZ)(?:"|$))"), QRegularExpression::CaseInsensitiveOption);
    if (partRar.match(subject).hasMatch() || rxx.match(subject).hasMatch()) { if (kind) *kind = ArchiveKind::Rar; return true; }
    if (seven.match(subject).hasMatch()) { if (kind) *kind = ArchiveKind::SevenZip; return true; }
    if (zip.match(subject).hasMatch()) { if (kind) *kind = ArchiveKind::Zip; return true; }
    if (tar.match(subject).hasMatch()) { if (kind) *kind = ArchiveKind::Tar; return true; }
    if (tgz.match(subject).hasMatch()) { if (kind) *kind = ArchiveKind::Tgz; return true; }
    Q_UNUSED(upper);
    return false;
}

QVector<NzbFile> parseNzbDocument(const QByteArray &xml, RemoteError *error)
{
    QVector<NzbFile> files;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QStringLiteral("file")) continue;
        NzbFile file;
        file.subject = reader.attributes().value(QStringLiteral("subject")).toString();
        QString group;
        while (!(reader.isEndElement() && reader.name() == QStringLiteral("file")) && !reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement() && reader.name() == QStringLiteral("group")) {
                const QString candidate = reader.readElementText().trimmed();
                if (group.isEmpty()) group = candidate;
            } else if (reader.isStartElement() && reader.name() == QStringLiteral("segment")) {
                NzbSegment segment;
                segment.number = reader.attributes().value(QStringLiteral("number")).toInt();
                segment.declaredBytes = reader.attributes().value(QStringLiteral("bytes")).toLongLong();
                segment.group = group;
                segment.article = reader.readElementText().trimmed();
                file.segments.push_back(segment);
            }
        }
        if (!file.segments.isEmpty()) {
            std::sort(file.segments.begin(), file.segments.end(), [](const NzbSegment &a, const NzbSegment &b) { return a.number < b.number; });
            const int maxNumber = file.segments.last().number;
            if (maxNumber > 0 && maxNumber < 1000000) {
                QVector<NzbSegment> dense;
                dense.reserve(maxNumber);
                int sourceIndex = 0;
                const qint64 defaultBytes = file.segments.first().declaredBytes;
                for (int number = 1; number <= maxNumber; ++number) {
                    if (sourceIndex < file.segments.size() && file.segments[sourceIndex].number == number) {
                        NzbSegment segment = file.segments[sourceIndex++];
                        if (segment.group.isEmpty()) segment.group = group;
                        dense.push_back(std::move(segment));
                    } else {
                        dense.push_back(NzbSegment{number, defaultBytes, group, QStringLiteral("nntp_nope"), true});
                    }
                }
                file.segments = std::move(dense);
            }
            file.filename = extractFileName(file.subject, QStringLiteral(R"(MKV|MP4|AVI|TS|RAR|R\d+|7Z(?:\.\d+)?|ZIP|Z\d+|TAR(?:\.\d+)?|TGZ(?:\.\d+)?|GZ)"));
            files.push_back(std::move(file));
        }
    }
    if (reader.hasError()) {
        if (error) *error = {RemoteErrorCode::Parser, reader.errorString(), 0};
        return {};
    }
    if (error) *error = {};
    return files;
}

QString nntpArticleArgument(QString article)
{
    article = article.trimmed();
    if (article.contains('@') && !article.startsWith('<')) return QLatin1Char('<') + article + QLatin1Char('>');
    return article;
}

QByteArray readLineBlocking(QAbstractSocket &socket, RemoteError *error)
{
    while (!socket.canReadLine()) {
        if (!socket.waitForReadyRead(10000)) {
            if (error) *error = {RemoteErrorCode::Transport, socket.errorString(), 0};
            return {};
        }
    }
    if (error) *error = {};
    return socket.readLine().trimmed();
}

int nntpCommand(QAbstractSocket &socket, const QByteArray &command, QByteArray *lineOut, RemoteError *error)
{
    if (!command.isEmpty()) {
        const QByteArray wire = command + "\r\n";
        if (socket.write(wire) != wire.size() || !socket.waitForBytesWritten(5000)) {
            if (error) *error = {RemoteErrorCode::Transport, socket.errorString(), 0};
            return 0;
        }
    }
    const QByteArray line = readLineBlocking(socket, error);
    if (lineOut) *lineOut = line;
    bool ok = false; const int code = line.left(3).toInt(&ok);
    if (!ok && error) *error = {RemoteErrorCode::Protocol, QStringLiteral("invalid NNTP reply"), 0};
    return ok ? code : 0;
}

QByteArray decodeYenc(const QList<QByteArray> &lines)
{
    bool inData = false;
    QByteArray out;
    for (QByteArray line : lines) {
        if (line.startsWith("=ybegin")) { inData = true; continue; }
        if (!inData) continue;
        if (line.startsWith("=ypart")) continue;
        if (line.startsWith("=yend")) break;
        for (int i = 0; i < line.size(); ++i) {
            unsigned char value = static_cast<unsigned char>(line[i]);
            if (value == '=') {
                if (++i >= line.size()) break;
                value = static_cast<unsigned char>(static_cast<unsigned char>(line[i]) - 64);
            }
            out.append(char(static_cast<unsigned char>(value - 42)));
        }
    }
    return out;
}

} // namespace

FtpProbe QtFtpBackend::probe(const QUrl &url, RemoteError *error)
{
    FtpProbe probe;
    FtpConnection connection;
    if (!connection.open(url, error)) return probe;
    const QString path = QUrl::fromPercentEncoding(url.path().toUtf8());
    FtpReply mdtm = connection.command(QStringLiteral("MDTM %1").arg(path), nullptr);
    if (mdtm.code == 213 && !mdtm.lines.isEmpty()) probe.lastModified = parseFtpTimestamp(mdtm.lines.last().mid(4));
    if (!probe.lastModified.isValid()) {
        FtpReply mlst = connection.command(QStringLiteral("MLST %1").arg(path), nullptr);
        for (const QString &line : mlst.lines) {
            static const QRegularExpression modify(QStringLiteral(R"(modify=(\d{14}))"), QRegularExpression::CaseInsensitiveOption);
            const auto match = modify.match(line);
            if (match.hasMatch()) { probe.lastModified = parseFtpTimestamp(match.captured(1)); break; }
        }
    }
    if (!probe.lastModified.isValid()) {
        connection.close();
        if (error) *error = {RemoteErrorCode::NotFound, QStringLiteral("FTP last-modified unavailable"), 550};
        return {};
    }
    FtpReply sizeReply = connection.command(QStringLiteral("SIZE %1").arg(path), error);
    if (sizeReply.code != 213 || sizeReply.lines.isEmpty()) { connection.close(); return {}; }
    bool sizeOk = false; probe.size = sizeReply.lines.last().mid(4).trimmed().toLongLong(&sizeOk);
    if (!sizeOk || probe.size < 0) {
        connection.close();
        if (error) *error = {RemoteErrorCode::Protocol, QStringLiteral("invalid FTP SIZE reply"), sizeReply.code};
        return {};
    }
    FtpReply features = connection.command(QStringLiteral("FEAT"), nullptr);
    for (const QString &line : features.lines) if (line.trimmed().startsWith(QStringLiteral("REST"), Qt::CaseInsensitive)) probe.supportsSeek = true;
    probe.mime = QString::fromUtf8(contentTypeForName(QFileInfo(path).fileName()));
    connection.close();
    if (error) *error = {};
    return probe;
}

RemoteRead QtFtpBackend::read(const QUrl &url, qint64 start, std::optional<qint64> endInclusive,
                              CancellationToken *cancel)
{
    RemoteRead result; result.start = start;
    RemoteError error;
    FtpProbe probe = this->probe(url, &error);
    if (!error.ok()) { result.error = error; return result; }
    result.totalLength = probe.size;
    const qint64 end = endInclusive.value_or(probe.size - 1);
    if (start < 0 || start >= probe.size || end < start || end >= probe.size) {
        result.error = {RemoteErrorCode::RangeNotSatisfiable, QStringLiteral("FTP range outside file"), 416};
        return result;
    }
    if (!probe.supportsSeek && start != 0) {
        result.error = {RemoteErrorCode::Unsupported, QStringLiteral("FTP server does not advertise REST"), 405};
        return result;
    }
    FtpConnection connection;
    if (!connection.open(url, &error)) { result.error = error; return result; }
    QByteArray bytes = connection.retrieve(QUrl::fromPercentEncoding(url.path().toUtf8()), start, cancel, &error);
    connection.close();
    if (!error.ok()) { result.error = error; return result; }
    const qint64 wanted = end - start + 1;
    if (bytes.size() > wanted) bytes.truncate(wanted);
    result.bytes = bytes; result.end = start + bytes.size() - 1;
    return result;
}

FtpService::FtpService(std::shared_ptr<FtpBackend> backend)
    : m_backend(std::move(backend))
{
    if (!m_backend) m_backend = std::make_shared<QtFtpBackend>();
}

QString FtpService::randomKey() const { return QUuid::createUuid().toString(QUuid::Id128).left(12); }

Response FtpService::handle(const Request &request)
{
    if (request.path == QStringLiteral("/create") || request.path.startsWith(QStringLiteral("/create/"))) return create(request);
    if (request.path == QStringLiteral("/stream")) return stream(request);
    if (request.path.startsWith(QStringLiteral("/stream/"))) return stream(request);
    // module 1306 also mounts ALL /:fileName as a create alias.
    if (request.path.count(QLatin1Char('/')) == 1 && request.path.size() > 1) return create(request);
    Response response; response.status = 404; return response;
}

Response FtpService::create(const Request &request)
{
    Response response;
    const bool post = request.method == "POST";
    QString ftpUrl;
    QString key;

    if (post) {
        const QJsonDocument document = QJsonDocument::fromJson(request.body);
        ftpUrl = document.isObject() ? document.object().value(QStringLiteral("ftpUrl")).toString() : QString();
        if (ftpUrl.isEmpty()) { response.status = 500; response.body = "Cannot parse JSON data, err 1"; return response; }
        if (request.path.startsWith(QStringLiteral("/create/"))) key = request.path.mid(QStringLiteral("/create/").size());
        if (key.isEmpty()) key = randomKey();
    } else {
        // Stremio module 1306: ALL /create and ALL /:fileName consume the
        // module-77 encoded bootstrap and use SHA-256(lz) as stable identity.
        const QString lz = request.query.queryItemValue(QStringLiteral("lz"));
        if (lz.isEmpty()) { response.status = 500; response.body = "Cannot parse JSON data, err 2"; return response; }
        const auto decoded = decompressLzEncodedURIComponent(lz);
        QJsonParseError parseError;
        const QJsonDocument document = decoded ? QJsonDocument::fromJson(decoded->toUtf8(), &parseError) : QJsonDocument();
        if (!decoded || parseError.error != QJsonParseError::NoError || !document.isObject()) {
            response.status = 500; response.body = "Cannot parse JSON data, err 3"; return response;
        }
        ftpUrl = document.object().value(QStringLiteral("ftpUrl")).toString();
        if (ftpUrl.isEmpty()) { response.status = 500; return response; }
        key = sha256Hex(lz);
    }

    if (m_sessions.contains(key)) {
        response.status = post ? 200 : 302;
        if (!post) { response.setHeader("Location", m_sessions.value(key).streamPath.toUtf8()); return response; }
        response.setHeader("Content-Type", "application/json");
        response.body = QJsonDocument(QJsonObject{{QStringLiteral("key"), key}}).toJson(QJsonDocument::Compact);
        response.setHeader("Content-Length", QByteArray::number(response.body.size()));
        return response;
    }

    RemoteError error;
    const QUrl url(ftpUrl);
    FtpProbe probe = m_backend->probe(url, &error);
    if (!error.ok() || probe.size < 0) { response.status = 500; return response; }
    const QString filename = QFileInfo(url.path()).fileName();
    Session session{url, probe, QStringLiteral("/stream/%1/%2").arg(key, filename)};
    m_sessions.insert(key, session);
    if (!post) {
        response.status = 302; response.setHeader("Location", session.streamPath.toUtf8()); return response;
    }
    response.status = 200; response.setHeader("Content-Type", "application/json");
    response.body = QJsonDocument(QJsonObject{{QStringLiteral("key"), key}}).toJson(QJsonDocument::Compact);
    response.setHeader("Content-Length", QByteArray::number(response.body.size()));
    return response;
}

Response FtpService::stream(const Request &request)
{
    Response response;
    if (request.path == QStringLiteral("/stream")) {
        const QString key = request.query.queryItemValue(QStringLiteral("key"));
        if (!m_sessions.contains(key)) { response.status = 500; return response; }
        response.status = 302; response.setHeader("Location", m_sessions.value(key).streamPath.toUtf8()); return response;
    }
    const QStringList parts = request.path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 3) { response.status = 500; return response; }
    const QString key = parts[1];
    if (!m_sessions.contains(key)) { response.status = 500; return response; }
    const Session &session = m_sessions[key];
    const QByteArray rangeRaw = request.header("Range");
    SimpleRange range = parseRange(rangeRaw, session.probe.size);
    if (range.present && !range.valid) { response.status = 416; return response; }
    if (!session.probe.supportsSeek && range.present && rangeRaw != "bytes=0-" &&
        rangeRaw != "bytes=0-" + QByteArray::number(session.probe.size - 1)) {
        response.status = 405; return response;
    }
    const qint64 start = range.present ? range.start : 0;
    const qint64 end = range.present ? range.end : session.probe.size - 1;
    response.status = range.present ? 206 : 200;
    response.setHeader("Access-Control-Allow-Origin", request.header("Origin").isEmpty() ? QByteArray("*") : request.header("Origin"));
    response.setHeader("Content-Length", QByteArray::number(end - start + 1));
    response.setHeader("Content-Type", session.probe.mime.toUtf8());
    response.setHeader("Accept-Ranges", session.probe.supportsSeek ? "bytes" : "none");
    response.setHeader("transferMode.dlna.org", kDlnaTransferMode);
    response.setHeader("contentFeatures.dlna.org", kDlnaFeatures);
    if (range.present) response.setHeader("Content-Range", "bytes " + QByteArray::number(start) + "-" + QByteArray::number(end) + "/" + QByteArray::number(session.probe.size));
    if (request.method == "HEAD") return response;
    CancellationToken token;
    RemoteRead read = m_backend->read(session.url, start, end, &token);
    if (!read.error.ok()) { response.status = read.error.transportStatus == 405 ? 405 : 500; response.body.clear(); return response; }
    response.body = read.bytes;
    return response;
}

std::optional<NntpEndpoint> parseNntpEndpoint(const QString &urlText)
{
    const QUrl url(urlText);
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("nntp") && scheme != QStringLiteral("nntps")) return std::nullopt;
    NntpEndpoint endpoint;
    endpoint.tls = scheme == QStringLiteral("nntps");
    endpoint.host = url.host();
    endpoint.port = static_cast<quint16>(url.port(endpoint.tls ? 563 : 119));
    endpoint.user = url.userName(QUrl::FullyDecoded);
    endpoint.password = url.password(QUrl::FullyDecoded);
    bool ok = false;
    const int connections = url.path().mid(1).toInt(&ok);
    endpoint.connections = ok && connections > 0 ? connections : 1;
    if (endpoint.host.isEmpty()) return std::nullopt;
    return endpoint;
}

HttpNzbDocumentFetcher::HttpNzbDocumentFetcher(QNetworkAccessManager *network) : m_network(network)
{
    if (!m_network) { m_ownedNetwork = std::make_unique<QNetworkAccessManager>(); m_network = m_ownedNetwork.get(); }
}

QByteArray HttpNzbDocumentFetcher::fetch(const QUrl &url, RemoteError *error)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });
    QEventLoop loop; QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit); loop.exec();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll(); const auto networkError = reply->error(); const QString message = reply->errorString();
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError || status >= 400) {
        if (error) *error = {RemoteErrorCode::Transport, message, status}; return {};
    }
    if (error) *error = {}; return body;
}

QByteArray QtNntpBackend::fetch(const NntpEndpoint &endpoint, const NzbSegment &segment, RemoteError *error)
{
    if (segment.syntheticMissing || segment.article == QStringLiteral("nntp_nope")) {
        if (error) *error = {RemoteErrorCode::NotFound, QStringLiteral("missing NZB segment"), 0};
        return {};
    }
    std::unique_ptr<QAbstractSocket> socket;
    if (endpoint.tls) {
        auto ssl = std::make_unique<QSslSocket>();
        QObject::connect(ssl.get(), &QSslSocket::sslErrors, ssl.get(), [ptr = ssl.get()](const QList<QSslError> &) { ptr->ignoreSslErrors(); });
        ssl->connectToHostEncrypted(endpoint.host, endpoint.port);
        if (!ssl->waitForEncrypted(10000)) { if (error) *error = {RemoteErrorCode::Transport, ssl->errorString(), 0}; return {}; }
        socket = std::move(ssl);
    } else {
        auto tcp = std::make_unique<QTcpSocket>(); tcp->connectToHost(endpoint.host, endpoint.port);
        if (!tcp->waitForConnected(10000)) { if (error) *error = {RemoteErrorCode::Transport, tcp->errorString(), 0}; return {}; }
        socket = std::move(tcp);
    }
    QByteArray replyLine;
    int code = nntpCommand(*socket, {}, &replyLine, error);
    if (code != 200 && code != 201) { if (error) *error = {RemoteErrorCode::Protocol, QStringLiteral("NNTP greeting rejected"), code}; return {}; }
    if (!endpoint.user.isEmpty()) {
        code = nntpCommand(*socket, "AUTHINFO USER " + endpoint.user.toUtf8(), &replyLine, error);
        if (code == 381) code = nntpCommand(*socket, "AUTHINFO PASS " + endpoint.password.toUtf8(), &replyLine, error);
        if (code != 281) { if (error) *error = {RemoteErrorCode::Protocol, QStringLiteral("NNTP authentication rejected"), code}; return {}; }
    }
    if (!segment.group.isEmpty()) {
        code = nntpCommand(*socket, "GROUP " + segment.group.toUtf8(), &replyLine, error);
        if (code != 211) { if (error) *error = {RemoteErrorCode::NotFound, QStringLiteral("NNTP group unavailable"), code}; return {}; }
    }
    code = nntpCommand(*socket, "BODY " + nntpArticleArgument(segment.article).toUtf8(), &replyLine, error);
    if (code != 222) { if (error) *error = {RemoteErrorCode::NotFound, QStringLiteral("NNTP article unavailable"), code}; return {}; }
    QList<QByteArray> lines;
    while (true) {
        QByteArray line = readLineBlocking(*socket, error);
        if (error && !error->ok()) return {};
        if (line == ".") break;
        if (line.startsWith("..")) line.remove(0, 1);
        lines.push_back(line);
    }
    nntpCommand(*socket, "QUIT", nullptr, nullptr);
    bool yenc = false; for (const auto &line : lines) if (line.startsWith("=ybegin")) { yenc = true; break; }
    if (error) *error = {};
    if (yenc) return decodeYenc(lines);
    QByteArray raw; for (int i = 0; i < lines.size(); ++i) { raw += lines[i]; if (i + 1 < lines.size()) raw += "\r\n"; }
    return raw;
}

NzbService::NzbService(std::shared_ptr<NzbDocumentFetcher> documents,
                       std::shared_ptr<NntpBackend> nntp,
                       ArchiveBootstrap archiveBootstrap)
    : m_documents(std::move(documents)), m_nntp(std::move(nntp)), m_archiveBootstrap(std::move(archiveBootstrap))
{
    if (!m_documents) m_documents = std::make_shared<HttpNzbDocumentFetcher>();
    if (!m_nntp) m_nntp = std::make_shared<QtNntpBackend>();
}

void NzbService::setLoopbackEndpoint(QString ip, quint16 port) { m_loopbackIp = std::move(ip); m_loopbackPort = port; }
QString NzbService::randomKey() const { return QUuid::createUuid().toString(QUuid::Id128).left(12); }

Response NzbService::handle(const Request &request)
{
    if (request.path == QStringLiteral("/create") || request.path.startsWith(QStringLiteral("/create/"))) return create(request);
    if (request.path == QStringLiteral("/stream")) return redirect(request);
    if (request.path.startsWith(QStringLiteral("/stream/"))) return stream(request);
    Response response; response.status = 404; return response;
}

QByteArray NzbService::fetchSegmentWithBackbones(const Session &session, const NzbSegment &segment,
                                                 bool zeroFillMissing, RemoteError *error)
{
    RemoteError lastError;
    for (const NntpEndpoint &endpoint : session.endpoints) {
        QByteArray bytes = m_nntp->fetch(endpoint, segment, &lastError);
        if (lastError.ok()) { if (error) *error = {}; return bytes; }
    }
    if (zeroFillMissing) {
        if (error) *error = {};
        return QByteArray(static_cast<qsizetype>(std::max<qint64>(0, segment.declaredBytes)), '\0');
    }
    if (error) *error = lastError.ok() ? RemoteError{RemoteErrorCode::NotFound, QStringLiteral("segment unavailable"), 0} : lastError;
    return {};
}

bool NzbService::initializeCandidate(const QVector<NntpEndpoint> &endpoints, const QUrl &nzbUrl,
                                     const QString &key, Session *session, RemoteError *error)
{
    RemoteError documentError;
    const QByteArray document = m_documents->fetch(nzbUrl, &documentError);
    if (!documentError.ok()) { if (error) *error = documentError; return false; }
    QVector<NzbFile> files = parseNzbDocument(document, &documentError);
    if (!documentError.ok()) { if (error) *error = documentError; return false; }

    ArchiveKind archiveKind = ArchiveKind::Zip;
    QVector<NzbFile> interesting;
    for (const NzbFile &file : files) {
        ArchiveKind candidate;
        if (archiveFileKind(file.subject, &candidate)) {
            if (interesting.isEmpty()) archiveKind = candidate;
            if (candidate == archiveKind) interesting.push_back(file);
        }
    }
    bool archive = !interesting.isEmpty();
    if (!archive) {
        for (const NzbFile &file : files) {
            const QString video = extractFileName(file.filename.isEmpty() ? file.subject : file.filename, QStringLiteral("MKV|MP4|AVI|TS"));
            if (!video.isEmpty()) { NzbFile copy = file; copy.filename = video; interesting.push_back(std::move(copy)); }
        }
    }
    if (interesting.isEmpty()) { if (error) *error = {RemoteErrorCode::NotFound, QStringLiteral("no interesting files"), 0}; return false; }

    qint64 totalPieces = 0, missingPieces = 0;
    for (const NzbFile &file : interesting) {
        for (const NzbSegment &segment : file.segments) { ++totalPieces; if (segment.syntheticMissing) ++missingPieces; }
        if (file.segments.isEmpty() || file.segments.first().syntheticMissing || file.segments.last().syntheticMissing) {
            if (error) *error = {RemoteErrorCode::NotFound, QStringLiteral("Missing initial pieces from a file in nzb metadata"), 0}; return false;
        }
    }
    if (totalPieces && double(missingPieces) / double(totalPieces) > 0.02) {
        if (error) *error = {RemoteErrorCode::NotFound, QStringLiteral("more than 2% of pieces are missing, breaking connection"), 0}; return false;
    }

    Session candidate;
    candidate.endpoints = endpoints; candidate.nzbUrl = nzbUrl; candidate.archive = archive; candidate.archiveKind = archiveKind;
    auto probeFile = [&](const NzbFile &file, qint64 *sizeOut, qint64 *chunkOut, qint64 *lastOut) -> bool {
        candidate.endpoints = endpoints;
        RemoteError firstError, lastError;
        QByteArray first = fetchSegmentWithBackbones(candidate, file.segments.first(), false, &firstError);
        if (!firstError.ok()) { if (error) *error = firstError; return false; }
        QByteArray last = file.segments.size() == 1 ? first : fetchSegmentWithBackbones(candidate, file.segments.last(), false, &lastError);
        if (file.segments.size() > 1 && !lastError.ok()) { if (error) *error = lastError; return false; }
        *chunkOut = first.size(); *lastOut = last.size();
        *sizeOut = (file.segments.size() - 1) * (*chunkOut) + (*lastOut);
        return true;
    };

    if (!archive) {
        NzbFile file = interesting.first();
        qint64 size = 0, chunk = 0, last = 0;
        if (!probeFile(file, &size, &chunk, &last)) return false;
        candidate.fileName = file.filename.isEmpty() ? extractFileName(file.subject, QStringLiteral("MKV|MP4|AVI|TS")) : file.filename;
        candidate.segments = file.segments; candidate.size = size; candidate.chunkSize = chunk; candidate.lastChunkSize = last;
        candidate.streamPath = QStringLiteral("/nzb/stream/%1/%2").arg(key, QString::fromUtf8(QUrl::toPercentEncoding(candidate.fileName)));
    } else {
        auto orderValue = [archiveKind](const NzbFile &file) -> qint64 {
            const QString name = file.filename.isEmpty() ? file.subject : file.filename;
            QRegularExpression re; qint64 def = 0;
            switch (archiveKind) {
            case ArchiveKind::SevenZip: re = QRegularExpression(QStringLiteral(R"(\.7Z\.(\d{1,5})$)"), QRegularExpression::CaseInsensitiveOption); break;
            case ArchiveKind::Zip: re = QRegularExpression(QStringLiteral(R"(\.Z(\d{1,5})$)"), QRegularExpression::CaseInsensitiveOption); def = 99999; break;
            case ArchiveKind::Rar: {
                if (name.endsWith(QStringLiteral(".rar"), Qt::CaseInsensitive)) return -1;
                re = QRegularExpression(QStringLiteral(R"(\.R(\d{1,5})$)"), QRegularExpression::CaseInsensitiveOption); break;
            }
            case ArchiveKind::Tar: re = QRegularExpression(QStringLiteral(R"(\.TAR\.(\d{1,5})$)"), QRegularExpression::CaseInsensitiveOption); break;
            case ArchiveKind::Tgz: re = QRegularExpression(QStringLiteral(R"(\.(\d{1,5})$)"), QRegularExpression::CaseInsensitiveOption); break;
            }
            const auto match = re.match(name); return match.hasMatch() ? match.captured(1).toLongLong() : def;
        };
        std::stable_sort(interesting.begin(), interesting.end(), [&](const NzbFile &a, const NzbFile &b) { return orderValue(a) < orderValue(b); });
        QVector<SourceSpec> bootstrap;
        for (NzbFile &file : interesting) {
            if (file.filename.isEmpty()) file.filename = extractFileName(file.subject, QStringLiteral(R"(RAR|R\d+|7Z(?:\.\d+)?|ZIP|Z\d+|TAR(?:\.\d+)?|TGZ(?:\.\d+)?|GZ)"));
            qint64 size = 0, chunk = 0, last = 0;
            if (!probeFile(file, &size, &chunk, &last)) return false;
            candidate.archiveFileNames.push_back(file.filename); candidate.archiveSegments.insert(file.filename, file.segments); candidate.archiveSizes.insert(file.filename, size);
            SourceSpec spec;
            spec.url = QUrl(QStringLiteral("http://%1:%2/nzb/stream/%3/%4").arg(m_loopbackIp).arg(m_loopbackPort).arg(key, QString::fromUtf8(QUrl::toPercentEncoding(file.filename))));
            spec.bytes = size; bootstrap.push_back(spec);
        }
        candidate.streamPath = QStringLiteral("/%1/stream?key=%2").arg(archiveKindName(archiveKind), key);
        if (m_archiveBootstrap) m_archiveBootstrap(archiveKind, key, bootstrap);
    }
    *session = std::move(candidate);
    if (error) *error = {};
    return true;
}

Response NzbService::create(const Request &request)
{
    Response response;
    const bool post = request.method == "POST";
    QJsonObject object;
    QString key;
    if (post) {
        const QJsonDocument doc = QJsonDocument::fromJson(request.body);
        if (!doc.isObject()) { response.status = 500; response.body = "Cannot parse JSON data, err 1"; return response; }
        object = doc.object();
        if (request.path.startsWith(QStringLiteral("/create/"))) key = request.path.mid(QStringLiteral("/create/").size());
        if (key.isEmpty()) key = randomKey();
    } else {
        // Stremio module 1088: GET/ALL bootstrap uses module-77 LZ data,
        // SHA-256(lz) identity, and redirects after successful initialization.
        const QString lz = request.query.queryItemValue(QStringLiteral("lz"));
        if (lz.isEmpty()) { response.status = 500; response.body = "Cannot parse JSON data, err 2"; return response; }
        const auto decoded = decompressLzEncodedURIComponent(lz);
        QJsonParseError parseError;
        const QJsonDocument doc = decoded ? QJsonDocument::fromJson(decoded->toUtf8(), &parseError) : QJsonDocument();
        if (!decoded || parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            response.status = 500; response.body = "Cannot parse JSON data, err 3"; return response;
        }
        object = doc.object();
        key = sha256Hex(lz);
    }
    QVector<NntpEndpoint> endpoints;
    for (const QJsonValue &value : object.value(QStringLiteral("servers")).toArray()) {
        const auto endpoint = parseNntpEndpoint(value.toString()); if (endpoint) endpoints.push_back(*endpoint);
    }
    QVector<QUrl> urls;
    if (object.value(QStringLiteral("nzbUrl")).isString()) urls.push_back(QUrl(object.value(QStringLiteral("nzbUrl")).toString()));
    for (const QJsonValue &value : object.value(QStringLiteral("nzbUrls")).toArray()) if (value.isString()) urls.push_back(QUrl(value.toString()));
    if (endpoints.isEmpty() || urls.isEmpty()) {
        response.status = 500;
        response.body = post ? QByteArray("Cannot parse JSON data, err 1") : QByteArray("Cannot parse JSON data, err 3");
        return response;
    }
    if (m_sessions.contains(key)) {
        if (!post) { response.status = 302; response.setHeader("Location", m_sessions.value(key).streamPath.toUtf8()); return response; }
        response.status = 200; response.setHeader("Content-Type", "application/json");
        response.body = QJsonDocument(QJsonObject{{QStringLiteral("key"), key}}).toJson(QJsonDocument::Compact);
        response.setHeader("Content-Length", QByteArray::number(response.body.size())); return response;
    }
    RemoteError lastError;
    Session session;
    bool initialized = false;
    for (const QUrl &url : urls) {
        if (initializeCandidate(endpoints, url, key, &session, &lastError)) { initialized = true; break; }
    }
    if (!initialized) { response.status = 500; response.body = "All nzb files failed checks, cannot play"; return response; }
    m_sessions.insert(key, session);
    if (!post) { response.status = 302; response.setHeader("Location", session.streamPath.toUtf8()); return response; }
    response.status = 200; response.setHeader("Content-Type", "application/json");
    response.body = QJsonDocument(QJsonObject{{QStringLiteral("key"), key}}).toJson(QJsonDocument::Compact);
    response.setHeader("Content-Length", QByteArray::number(response.body.size()));
    return response;
}

Response NzbService::redirect(const Request &request)
{
    Response response;
    const QString key = request.query.queryItemValue(QStringLiteral("key"));
    if (!m_sessions.contains(key)) { response.status = 500; response.body = "Cannot stream this nzb file"; return response; }
    response.status = 302; response.setHeader("Location", m_sessions.value(key).streamPath.toUtf8()); return response;
}

Response NzbService::stream(const Request &request)
{
    Response response;
    const QStringList path = request.path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (path.size() < 3) { response.status = 500; return response; }
    const QString key = path[1]; const QString fileName = QUrl::fromPercentEncoding(path.mid(2).join(QLatin1Char('/')).toUtf8());
    if (!m_sessions.contains(key)) { response.status = 500; return response; }
    Session session = m_sessions.value(key);
    QVector<NzbSegment> segments; qint64 size = 0; qint64 chunkSize = 0; qint64 lastChunkSize = 0;
    if (session.archive) {
        if (!session.archiveSegments.contains(fileName)) { response.status = 500; return response; }
        segments = session.archiveSegments.value(fileName); size = session.archiveSizes.value(fileName);
        RemoteError firstError, lastError;
        const QByteArray first = fetchSegmentWithBackbones(session, segments.first(), false, &firstError);
        const QByteArray last = segments.size() == 1 ? first : fetchSegmentWithBackbones(session, segments.last(), false, &lastError);
        if (!firstError.ok() || (segments.size() > 1 && !lastError.ok())) { response.status = 500; return response; }
        chunkSize = first.size(); lastChunkSize = last.size();
    } else {
        if (fileName != session.fileName) { response.status = 500; return response; }
        segments = session.segments; size = session.size; chunkSize = session.chunkSize; lastChunkSize = session.lastChunkSize;
    }
    SimpleRange range = parseRange(request.header("Range"), size);
    if (range.present && !range.valid) { response.status = 416; response.setHeader("Content-Range", "bytes */" + QByteArray::number(size)); return response; }
    const qint64 start = range.present ? range.start : 0; const qint64 end = range.present ? range.end : size - 1;
    response.status = 200; // exact module 1088 quirk: ranged NZB streaming still reports 200.
    response.setHeader("Access-Control-Allow-Origin", request.header("Origin").isEmpty() ? QByteArray("*") : request.header("Origin"));
    response.setHeader("Content-Length", QByteArray::number(end - start + 1));
    response.setHeader("Content-Type", contentTypeForName(fileName));
    response.setHeader("Accept-Ranges", "bytes"); response.setHeader("transferMode.dlna.org", kDlnaTransferMode); response.setHeader("contentFeatures.dlna.org", kDlnaFeatures);
    if (range.present) response.setHeader("Content-Range", "bytes " + QByteArray::number(start) + "-" + QByteArray::number(end) + "/" + QByteArray::number(size));
    if (request.method == "HEAD") return response;

    QByteArray output;
    qint64 logicalOffset = 0;
    for (int i = 0; i < segments.size(); ++i) {
        const qint64 expected = (i == segments.size() - 1) ? lastChunkSize : chunkSize;
        const qint64 segmentStart = logicalOffset; const qint64 segmentEnd = logicalOffset + expected - 1; logicalOffset += expected;
        if (end < segmentStart) break; if (start > segmentEnd) continue;
        RemoteError fetchError;
        QByteArray bytes = fetchSegmentWithBackbones(session, segments[i], true, &fetchError);
        if (!fetchError.ok()) { response.status = 500; response.body.clear(); return response; }
        if (bytes.size() < expected) bytes += QByteArray(expected - bytes.size(), '\0');
        if (bytes.size() > expected) bytes.truncate(expected);
        const qint64 localStart = std::max<qint64>(0, start - segmentStart);
        const qint64 localEnd = std::min<qint64>(expected - 1, end - segmentStart);
        output += bytes.mid(localStart, localEnd - localStart + 1);
    }
    response.body = output;
    return response;
}

} // namespace Colosseum::Server::RemoteArchive
