#include "subtitlesidecar.h"

#include <QRegularExpression>
#include <QStringConverter>
#include <QStringList>

#include <algorithm>

namespace Colosseum::Player {

namespace {

// --- minimal inflate (RFC 1951), after zlib's puff.c ---------------------------------

struct BitReader {
    const uchar *data = nullptr;
    qsizetype size = 0;
    qsizetype pos = 0;
    quint32 bitBuf = 0;
    int bitCnt = 0;
    bool failed = false;

    int bits(int need)
    {
        quint32 val = bitBuf;
        while (bitCnt < need) {
            if (pos >= size) { failed = true; return 0; }
            val |= quint32(data[pos++]) << bitCnt;
            bitCnt += 8;
        }
        bitBuf = val >> need;
        bitCnt -= need;
        return int(val & ((1u << need) - 1));
    }
};

constexpr int kMaxBits = 15;

struct Huffman {
    short count[kMaxBits + 1] = {};
    short symbol[320] = {};
};

// Returns 0 for a complete code, >0 for an incomplete one, <0 for an over-subscribed one.
int buildHuffman(Huffman *h, const short *length, int n)
{
    for (int len = 0; len <= kMaxBits; ++len)
        h->count[len] = 0;
    for (int s = 0; s < n; ++s)
        h->count[length[s]]++;
    if (h->count[0] == n)
        return 0;
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return left;
    }
    short offs[kMaxBits + 1];
    offs[1] = 0;
    for (int len = 1; len < kMaxBits; ++len)
        offs[len + 1] = short(offs[len] + h->count[len]);
    for (int s = 0; s < n; ++s)
        if (length[s] != 0)
            h->symbol[offs[length[s]]++] = short(s);
    return left;
}

int decodeSymbol(BitReader *br, const Huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
        code |= br->bits(1);
        if (br->failed)
            return -1;
        const int count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

bool inflateCodes(BitReader *br, QByteArray *out, const Huffman *lencode, const Huffman *distcode)
{
    static const short lens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                   35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                   3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                    8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                   7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    constexpr qsizetype kMaxOutput = 64 * 1024 * 1024; // a subtitle file is never this big
    for (;;) {
        int symbol = decodeSymbol(br, lencode);
        if (symbol < 0)
            return false;
        if (symbol < 256) {
            out->append(char(symbol));
        } else if (symbol == 256) {
            return true;
        } else {
            symbol -= 257;
            if (symbol >= 29)
                return false;
            const int len = lens[symbol] + br->bits(lext[symbol]);
            const int ds = decodeSymbol(br, distcode);
            if (ds < 0 || ds >= 30)
                return false;
            const int dist = dists[ds] + br->bits(dext[ds]);
            if (br->failed || dist > out->size())
                return false;
            for (int i = 0; i < len; ++i)
                out->append(out->at(out->size() - dist));
        }
        if (br->failed || out->size() > kMaxOutput)
            return false;
    }
}

bool inflateRaw(const uchar *data, qsizetype size, QByteArray *out)
{
    BitReader br;
    br.data = data;
    br.size = size;
    int last = 0;
    do {
        last = br.bits(1);
        const int type = br.bits(2);
        if (br.failed)
            return false;
        if (type == 0) {
            br.bitBuf = 0;
            br.bitCnt = 0;
            if (br.pos + 4 > br.size)
                return false;
            const int len = data[br.pos] | (data[br.pos + 1] << 8);
            const int nlen = data[br.pos + 2] | (data[br.pos + 3] << 8);
            br.pos += 4;
            if (len != (~nlen & 0xffff) || br.pos + len > br.size)
                return false;
            out->append(reinterpret_cast<const char *>(data + br.pos), len);
            br.pos += len;
        } else if (type == 1 || type == 2) {
            Huffman lencode, distcode;
            if (type == 1) {
                short lengths[288];
                int s = 0;
                for (; s < 144; ++s) lengths[s] = 8;
                for (; s < 256; ++s) lengths[s] = 9;
                for (; s < 280; ++s) lengths[s] = 7;
                for (; s < 288; ++s) lengths[s] = 8;
                buildHuffman(&lencode, lengths, 288);
                short dl[30];
                for (s = 0; s < 30; ++s) dl[s] = 5;
                buildHuffman(&distcode, dl, 30);
            } else {
                static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
                const int nlen = br.bits(5) + 257;
                const int ndist = br.bits(5) + 1;
                const int ncode = br.bits(4) + 4;
                if (br.failed || nlen > 286 || ndist > 30)
                    return false;
                short lengths[320] = {};
                for (int i = 0; i < ncode; ++i)
                    lengths[order[i]] = short(br.bits(3));
                if (br.failed)
                    return false;
                Huffman codeLen;
                if (buildHuffman(&codeLen, lengths, 19) != 0)
                    return false;
                int index = 0;
                short ll[320] = {};
                while (index < nlen + ndist) {
                    const int symbol = decodeSymbol(&br, &codeLen);
                    if (symbol < 0)
                        return false;
                    if (symbol < 16) {
                        ll[index++] = short(symbol);
                    } else {
                        int len = 0, rep = 0;
                        if (symbol == 16) {
                            if (index == 0)
                                return false;
                            len = ll[index - 1];
                            rep = 3 + br.bits(2);
                        } else if (symbol == 17) {
                            rep = 3 + br.bits(3);
                        } else {
                            rep = 11 + br.bits(7);
                        }
                        if (br.failed || index + rep > nlen + ndist)
                            return false;
                        while (rep--)
                            ll[index++] = short(len);
                    }
                }
                if (ll[256] == 0)
                    return false;
                const int e1 = buildHuffman(&lencode, ll, nlen);
                if (e1 < 0 || (e1 > 0 && nlen - lencode.count[0] != 1))
                    return false;
                const int e2 = buildHuffman(&distcode, ll + nlen, ndist);
                if (e2 < 0 || (e2 > 0 && ndist - distcode.count[0] != 1))
                    return false;
            }
            if (!inflateCodes(&br, out, &lencode, &distcode))
                return false;
        } else {
            return false;
        }
    } while (!last);
    return true;
}

// --- text helpers -----------------------------------------------------------------

// [hh:]mm:ss[,.]fff -> ms, or -1.
qint64 parseTimestamp(const QString &token)
{
    static const QRegularExpression re(
        QStringLiteral("^(?:(\\d+):)?(\\d{1,2}):(\\d{1,2})(?:[,.](\\d{1,9}))?$"));
    const QRegularExpressionMatch m = re.match(token);
    if (!m.hasMatch())
        return -1;
    const qint64 h = m.captured(1).toLongLong();
    const qint64 min = m.captured(2).toLongLong();
    const qint64 s = m.captured(3).toLongLong();
    QString frac = m.captured(4).left(3);
    while (frac.size() < 3)
        frac.append(QLatin1Char('0'));
    return ((h * 60 + min) * 60 + s) * 1000 + frac.toLongLong();
}

QString cleanCueText(const QStringList &lines)
{
    static const QRegularExpression tags(QStringLiteral("<[^>]*>|\\{\\\\[^}]*\\}"));
    QStringList kept;
    for (QString line : lines) {
        line.remove(tags);
        line.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
        line.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
        line.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        line.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        line = line.trimmed();
        if (!line.isEmpty())
            kept.append(line);
    }
    return kept.join(QLatin1Char('\n'));
}

} // namespace

bool gunzipBytes(const QByteArray &gz, QByteArray *out)
{
    const uchar *d = reinterpret_cast<const uchar *>(gz.constData());
    const qsizetype n = gz.size();
    if (n < 18 || d[0] != 0x1f || d[1] != 0x8b || d[2] != 8)
        return false;
    const uchar flags = d[3];
    qsizetype pos = 10;
    if (flags & 4) { // FEXTRA
        if (pos + 2 > n)
            return false;
        pos += 2 + (d[pos] | (d[pos + 1] << 8));
    }
    for (const uchar bit : {uchar(8), uchar(16)}) { // FNAME, FCOMMENT: zero-terminated
        if (flags & bit) {
            while (pos < n && d[pos] != 0)
                ++pos;
            ++pos;
        }
    }
    if (flags & 2) // FHCRC
        pos += 2;
    if (pos >= n)
        return false;
    QByteArray result;
    if (!inflateRaw(d + pos, n - pos, &result))
        return false;
    *out = std::move(result);
    return true;
}

QString decodeSubtitleBytes(const QByteArray &input)
{
    QByteArray bytes = input;
    if (bytes.size() >= 2 && uchar(bytes[0]) == 0x1f && uchar(bytes[1]) == 0x8b) {
        QByteArray inflated;
        if (!gunzipBytes(bytes, &inflated))
            return {};
        bytes = inflated;
    }
    if (bytes.size() >= 2 && ((uchar(bytes[0]) == 0xff && uchar(bytes[1]) == 0xfe)
                              || (uchar(bytes[0]) == 0xfe && uchar(bytes[1]) == 0xff))) {
        QStringDecoder utf16(QStringConverter::Utf16); // consumes the BOM
        return utf16.decode(bytes);
    }
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString text = utf8.decode(bytes);
    if (!utf8.hasError())
        return text;
    return QString::fromLatin1(bytes);
}

QList<SidecarCue> parseSidecarSubtitles(const QString &input)
{
    QString text = input;
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = text.split(QLatin1Char('\n'));

    static const QRegularExpression timing(QStringLiteral("^\\s*(\\S+)\\s*-->\\s*(\\S+)"));
    QList<SidecarCue> cues;
    bool skipBlock = false;   // inside a NOTE / STYLE / REGION / header block
    bool inCue = false;
    qint64 start = 0, end = 0;
    QStringList body;

    auto flush = [&]() {
        if (inCue) {
            const QString clean = cleanCueText(body);
            if (!clean.isEmpty() && end > start)
                cues.append({start, end, clean});
        }
        inCue = false;
        body.clear();
    };

    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            flush();
            skipBlock = false;
            continue;
        }
        if (inCue) {
            body.append(line);
            continue;
        }
        if (skipBlock)
            continue;
        const QRegularExpressionMatch m = timing.match(line);
        if (m.hasMatch()) {
            const qint64 s = parseTimestamp(m.captured(1));
            const qint64 e = parseTimestamp(m.captured(2));
            if (s >= 0 && e >= 0) {
                inCue = true;
                start = s;
                end = e;
                continue;
            }
        }
        // A cue identifier line falls through; headers and note blocks are skipped whole.
        if (line.startsWith(QStringLiteral("NOTE")) || line.startsWith(QStringLiteral("STYLE"))
                || line.startsWith(QStringLiteral("REGION")) || line.startsWith(QStringLiteral("WEBVTT")))
            skipBlock = true;
    }
    flush();

    std::stable_sort(cues.begin(), cues.end(),
                     [](const SidecarCue &a, const SidecarCue &b) { return a.startMs < b.startMs; });
    return cues;
}

QList<int> activeSidecarCues(const QList<SidecarCue> &cues, qint64 timeMs)
{
    QList<int> active;
    for (int i = 0; i < cues.size(); ++i) {
        if (cues.at(i).startMs > timeMs)
            break;
        if (timeMs < cues.at(i).endMs)
            active.append(i);
    }
    return active;
}

} // namespace Colosseum::Player
