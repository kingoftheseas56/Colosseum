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

struct EbmlElement { QByteArray id; qsizetype data = 0; qsizetype end = 0; };

bool readEbmlVint(const QByteArray &b, qsizetype off, quint64 *value, int *length)
{
    if (off < 0 || off >= b.size()) return false;
    const uchar first = uchar(b.at(off));
    uchar marker = 0x80;
    int len = 1;
    while (len <= 8 && !(first & marker)) { marker >>= 1; ++len; }
    if (len > 8 || off + len > b.size()) return false;
    quint64 v = first & quint64(marker - 1);
    for (int i = 1; i < len; ++i) v = (v << 8) | uchar(b.at(off + i));
    if (value) *value = v;
    if (length) *length = len;
    return true;
}

bool readEbmlElement(const QByteArray &b, qsizetype off, qsizetype limit,
                     EbmlElement *element, qsizetype *next)
{
    quint64 ignored = 0, dataSize = 0;
    int idLength = 0, sizeLength = 0;
    if (!readEbmlVint(b, off, &ignored, &idLength)
        || !readEbmlVint(b, off + idLength, &dataSize, &sizeLength)) return false;
    const qsizetype data = off + idLength + sizeLength;
    if (data > limit || dataSize > quint64(limit - data)) return false;
    if (element) *element = {b.mid(off, idLength).toHex().toUpper(), data,
                             data + qsizetype(dataSize)};
    if (next) *next = data + qsizetype(dataSize);
    return true;
}

QVector<EbmlElement> ebmlElements(const QByteArray &b, qsizetype begin, qsizetype end)
{
    QVector<EbmlElement> out;
    for (qsizetype off = begin; off < end;) {
        EbmlElement element;
        qsizetype next = off;
        if (!readEbmlElement(b, off, end, &element, &next) || next <= off) break;
        out.append(element);
        off = next;
    }
    return out;
}

const EbmlElement *findEbml(const QVector<EbmlElement> &list, const QByteArray &id)
{
    for (const EbmlElement &element : list) if (element.id == id) return &element;
    return nullptr;
}

quint64 ebmlUInt(const QByteArray &b, const EbmlElement *element)
{
    if (!element || element->end <= element->data || element->end - element->data > 8) return 0;
    quint64 value = 0;
    for (qsizetype off = element->data; off < element->end; ++off)
        value = (value << 8) | uchar(b.at(off));
    return value;
}

QString ebmlString(const QByteArray &b, const EbmlElement *element)
{
    return element ? QString::fromUtf8(b.mid(element->data, element->end - element->data)) : QString();
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

QVector<TrackInfo> parseMatroska(const QByteArray &bytes, QString *error)
{
    QVector<TrackInfo> result;
    const QVector<EbmlElement> root = ebmlElements(bytes, 0, bytes.size());
    const EbmlElement *segment = findEbml(root, QByteArrayLiteral("18538067"));
    if (!segment) { setError(error, QStringLiteral("This file type is not supported")); return result; }
    const QVector<EbmlElement> segmentElements = ebmlElements(bytes, segment->data, segment->end);
    const EbmlElement *tracks = findEbml(segmentElements, QByteArrayLiteral("1654AE6B"));
    if (!tracks) { setError(error, QStringLiteral("This file type is not supported")); return result; }

    const QVector<EbmlElement> entries = ebmlElements(bytes, tracks->data, tracks->end);
    for (const EbmlElement &entry : entries) {
        if (entry.id != QByteArrayLiteral("AE")) continue;
        const QVector<EbmlElement> fields = ebmlElements(bytes, entry.data, entry.end);
        const EbmlElement *trackNumber = findEbml(fields, QByteArrayLiteral("D7"));
        const EbmlElement *trackType = findEbml(fields, QByteArrayLiteral("83"));
        const EbmlElement *language = findEbml(fields, QByteArrayLiteral("22B59C"));
        const EbmlElement *languageBcp47 = findEbml(fields, QByteArrayLiteral("22B59D"));
        const EbmlElement *name = findEbml(fields, QByteArrayLiteral("536E"));
        const EbmlElement *codecId = findEbml(fields, QByteArrayLiteral("86"));

        TrackInfo info;
        if (trackNumber) info.id = int(ebmlUInt(bytes, trackNumber));
        const quint64 type = ebmlUInt(bytes, trackType);
        if (type == 1) info.type = QStringLiteral("video");
        else if (type == 2) info.type = QStringLiteral("audio");
        else if (type == 17) info.type = QStringLiteral("text");

        QString lang = ebmlString(bytes, language);
        if (lang == QStringLiteral("und")) lang.clear();
        if (lang.isEmpty()) {
            lang = ebmlString(bytes, languageBcp47);
            if (lang == QStringLiteral("und")) lang.clear();
        }
        if (!lang.isEmpty()) info.language = lang;
        const QString label = ebmlString(bytes, name);
        if (!label.isEmpty()) info.label = label;
        QString codec = ebmlString(bytes, codecId);
        if (codec.startsWith(QStringLiteral("V_")) || codec.startsWith(QStringLiteral("A_"))
            || codec.startsWith(QStringLiteral("S_"))) codec.remove(0, 2);
        if (!codec.isEmpty()) info.codec = codec;
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
        && uchar(bytes[2]) == 0xdf && uchar(bytes[3]) == 0xa3)
        return parseMatroska(bytes, error);
    setError(error, QStringLiteral("This file type is not supported"));
    return {};
}

} // namespace ColosseumServer::Media
