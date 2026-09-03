#include "colosseum_server/media/MediaPipeline.h"

#include <QFile>

namespace ColosseumServer::Media {
namespace {

void setError(QString *error, const QString &value)
{
    if (error) *error = value;
}

quint32 be32(const QByteArray &b, qsizetype off)
{
    if (off < 0 || off + 4 > b.size()) return 0;
    const auto *p = reinterpret_cast<const uchar *>(b.constData() + off);
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

struct Atom { QByteArray type; qsizetype data = 0; qsizetype end = 0; };

QVector<Atom> atoms(const QByteArray &b, qsizetype begin, qsizetype end)
{
    QVector<Atom> out;
    for (qsizetype off = begin; off + 8 <= end;) {
        quint64 size = be32(b, off);
        const QByteArray type = b.mid(off + 4, 4);
        qsizetype header = 8;
        if (size == 1 && off + 16 <= end) {
            size = (quint64(be32(b, off + 8)) << 32) | be32(b, off + 12);
            header = 16;
        } else if (size == 0) size = quint64(end - off);
        if (size < quint64(header) || size > quint64(end - off)) break;
        out.append({type, off + header, off + qsizetype(size)});
        off += qsizetype(size);
    }
    return out;
}
const Atom *findAtom(const QVector<Atom> &list, const QByteArray &type)
{
    for (const Atom &atom : list) if (atom.type == type) return &atom;
    return nullptr;
}

QVector<TrackInfo> parseMp4(const QByteArray &bytes, QString *error)
{
    QVector<TrackInfo> result;
    const QVector<Atom> root = atoms(bytes, 0, bytes.size());
    const Atom *moov = findAtom(root, QByteArrayLiteral("moov"));
    if (!moov) { setError(error, QStringLiteral("moov atom missing")); return result; }
    const QVector<Atom> moovAtoms = atoms(bytes, moov->data, moov->end);
    for (const Atom &trak : moovAtoms) {
        if (trak.type != QByteArrayLiteral("trak")) continue;
        const QVector<Atom> trackAtoms = atoms(bytes, trak.data, trak.end);
        const Atom *tkhd = findAtom(trackAtoms, QByteArrayLiteral("tkhd"));
        const Atom *mdia = findAtom(trackAtoms, QByteArrayLiteral("mdia"));
        if (!tkhd || !mdia) continue;
        TrackInfo info;
        const uchar version = uchar(bytes.at(tkhd->data));
        info.id = int(be32(bytes, tkhd->data + (version == 1 ? 20 : 12)));
        const QVector<Atom> mdiaAtoms = atoms(bytes, mdia->data, mdia->end);
        const Atom *hdlr = findAtom(mdiaAtoms, QByteArrayLiteral("hdlr"));
        const Atom *minf = findAtom(mdiaAtoms, QByteArrayLiteral("minf"));
        if (!hdlr || !minf) continue;
        const QByteArray handler = bytes.mid(hdlr->data + 8, 4);
        if (handler == QByteArrayLiteral("vide")) info.type = QStringLiteral("video");
        else if (handler == QByteArrayLiteral("soun")) info.type = QStringLiteral("audio");
        else if (handler == QByteArrayLiteral("text") || handler == QByteArrayLiteral("sbtl"))
            info.type = QStringLiteral("text");
        else continue;
        const QVector<Atom> minfAtoms = atoms(bytes, minf->data, minf->end);
        const Atom *stbl = findAtom(minfAtoms, QByteArrayLiteral("stbl"));
        if (!stbl) continue;
        const QVector<Atom> stblAtoms = atoms(bytes, stbl->data, stbl->end);
        const Atom *stsd = findAtom(stblAtoms, QByteArrayLiteral("stsd"));
        if (stsd && stsd->data + 16 <= stsd->end) {
            const QByteArray codec = bytes.mid(stsd->data + 12, 4).toUpper();
            info.codec = QString::fromLatin1(codec).replace(QLatin1Char('-'), QString());
        }
        result.append(info);
    }
    setError(error, {});
    return result;
}

} // namespace

QVector<TrackInfo> TrackParser::parseFile(const QString &path, qint64 maxBytes, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return {};
    }
    if (file.size() > maxBytes) {
        setError(error, QStringLiteral("Reached maxBytesLimit of %1").arg(maxBytes));
        return {};
    }
    return parseBytes(file.readAll(), error);
}

QVector<TrackInfo> TrackParser::parseBytes(const QByteArray &bytes, QString *error)
{
    if (bytes.size() >= 12 && bytes.mid(4, 4) == QByteArrayLiteral("ftyp"))
        return parseMp4(bytes, error);
    if (bytes.size() >= 4 && uchar(bytes[0]) == 0x1a && uchar(bytes[1]) == 0x45
        && uchar(bytes[2]) == 0xdf && uchar(bytes[3]) == 0xa3) {
        QVector<TrackInfo> tracks;
        const QByteArrayList prefixes{QByteArrayLiteral("V_"), QByteArrayLiteral("A_"), QByteArrayLiteral("S_")};
        for (const QByteArray &prefix : prefixes) {
            qsizetype pos = 0;
            while ((pos = bytes.indexOf(prefix, pos)) >= 0) {
                qsizetype end = pos;
                while (end < bytes.size() && end - pos < 64) {
                    const uchar c = uchar(bytes[end]);
                    if (c < 0x20 || c > 0x7e) break;
                    ++end;
                }
                const QByteArray codec = bytes.mid(pos, end - pos);
                if (codec.size() > 2) {
                    TrackInfo info;
                    info.id = tracks.size() + 1;
                    info.type = prefix == QByteArrayLiteral("V_") ? QStringLiteral("video")
                              : prefix == QByteArrayLiteral("A_") ? QStringLiteral("audio")
                              : QStringLiteral("text");
                    info.codec = QString::fromLatin1(codec.mid(2));
                    bool duplicate = false;
                    for (const TrackInfo &track : tracks)
                        duplicate = duplicate || (track.type == info.type && track.codec == info.codec);
                    if (!duplicate) tracks.append(info);
                }
                pos = qMax(end, pos + 2);
            }
        }
        if (!tracks.isEmpty()) { setError(error, {}); return tracks; }
    }
    setError(error, QStringLiteral("This file type is not supported"));
    return {};
}

} // namespace ColosseumServer::Media
