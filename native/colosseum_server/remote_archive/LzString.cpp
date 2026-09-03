#include "LzString.h"

#include <QVector>

namespace Colosseum::Server::RemoteArchive {

std::optional<QString> decompressLzEncodedURIComponent(QString input)
{
    // Exact alphabet/reset value used by Stremio 4.20.17 module 77
    // (lz-string decompressFromEncodedURIComponent).
    static const QString alphabet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-$");
    if (input.isNull()) return QString();
    if (input.isEmpty()) return std::nullopt;
    input.replace(QLatin1Char(' '), QLatin1Char('+'));

    auto baseValue = [&](int index) -> int {
        if (index < 0 || index >= input.size()) return -1;
        return alphabet.indexOf(input.at(index));
    };

    struct Data {
        int value = 0;
        int position = 32;
        int index = 1;
    } data;
    data.value = baseValue(0);
    if (data.value < 0) return std::nullopt;

    auto readBits = [&](int count, int *out) -> bool {
        int bits = 0;
        int power = 1;
        const int maxPower = 1 << count;
        while (power != maxPower) {
            const int resb = data.value & data.position;
            data.position >>= 1;
            if (data.position == 0) {
                data.position = 32;
                if (data.index >= input.size()) {
                    // The current bit was valid, but another character is only
                    // required if more bits remain in this read.
                    if ((power << 1) != maxPower) return false;
                    data.value = 0;
                } else {
                    data.value = baseValue(data.index++);
                    if (data.value < 0) return false;
                }
            }
            if (resb) bits |= power;
            power <<= 1;
        }
        *out = bits;
        return true;
    };

    QVector<QString> dictionary(4);
    int enlargeIn = 4;
    int dictSize = 4;
    int numBits = 3;

    int marker = 0;
    if (!readBits(2, &marker)) return std::nullopt;
    QString c;
    if (marker == 0) {
        int value = 0;
        if (!readBits(8, &value)) return std::nullopt;
        c = QChar(static_cast<ushort>(value));
    } else if (marker == 1) {
        int value = 0;
        if (!readBits(16, &value)) return std::nullopt;
        c = QChar(static_cast<ushort>(value));
    } else if (marker == 2) {
        return QString();
    } else {
        return std::nullopt;
    }

    dictionary[3] = c;
    QString w = c;
    QString result = c;

    for (;;) {
        int code = 0;
        if (!readBits(numBits, &code)) return std::nullopt;

        if (code == 0 || code == 1) {
            int value = 0;
            if (!readBits(code == 0 ? 8 : 16, &value)) return std::nullopt;
            if (dictionary.size() <= dictSize) dictionary.resize(dictSize + 1);
            dictionary[dictSize] = QChar(static_cast<ushort>(value));
            code = dictSize++;
            --enlargeIn;
        } else if (code == 2) {
            return result;
        }

        if (enlargeIn == 0) {
            enlargeIn = 1 << numBits;
            ++numBits;
        }

        QString entry;
        if (code >= 0 && code < dictionary.size() && !dictionary[code].isNull()) {
            entry = dictionary[code];
        } else if (code == dictSize) {
            if (w.isEmpty()) return std::nullopt;
            entry = w + w.left(1);
        } else {
            return std::nullopt;
        }

        result += entry;
        if (dictionary.size() <= dictSize) dictionary.resize(dictSize + 1);
        dictionary[dictSize++] = w + entry.left(1);
        w = entry;
        --enlargeIn;
        if (enlargeIn == 0) {
            enlargeIn = 1 << numBits;
            ++numBits;
        }
    }
}

} // namespace Colosseum::Server::RemoteArchive
