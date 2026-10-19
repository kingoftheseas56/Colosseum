// Charset selection is translated from M748's ordered recognizers, not a modern
// detector.
#include "server1/media/SubtitleFetch.h"
#include <algorithm>
#include <cmath>
#include <iterator>
namespace server1::media {
namespace {
struct SingleTable {
  const char *name;
  const char *c1Name;
  const int *map;
  const int *ngrams;
};
#include "SubtitleCharsetTables.inc"
int byte(const QByteArray &b, int i) {
  return i >= 0 && i < b.size() ? quint8(b[i]) : 0;
}
double single(const QByteArray &b, const SingleTable &t) {
  quint32 gram = 0;
  int count = 0, hits = 0;
  bool space = false;
  auto add = [&](int c) {
    gram = ((gram << 8) + (c & 255)) & 0xffffff;
    ++count;
    if (std::binary_search(t.ngrams, t.ngrams + 64, gram))
      ++hits;
  };
  for (auto c : b) {
    int m = t.map[quint8(c)];
    if (m) {
      if (m != 32 || !space)
        add(m);
      space = m == 32;
    }
  }
  add(32);
  double p = double(hits) / count;
  return p > .33 ? 98 : 300 * p;
}
double multi(const QByteArray &b, int kind, const int *common, int commonSize) {
  int pos = 0, total = 0, doubles = 0, bad = 0, hits = 0;
  bool done = false;
  auto next = [&]() {
    if (pos >= b.size()) {
      done = true;
      return -1;
    }
    return byte(b, pos++);
  };
  while (true) {
    bool error = false;
    int first = next(), cv = first;
    if (first < 0)
      break;
    if (kind == 759 || kind == 755) {
      if (!(first <= 127 ||
            (kind == 759 ? (first > 160 && first <= 223) : first == 255))) {
        int second = next();
        if (second < 0)
          break;
        cv = (first << 8) | second;
        error = kind == 759 ? second < 64
                            : (second < 64 || second == 127 || second == 255);
      }
    } else if (kind == 758) {
      if (first > 128) {
        int second = next();
        cv = (cv << 8) | second;
        if (first >= 129 && first <= 254) {
          if (!((second >= 64 && second <= 126) ||
                (second >= 80 && second <= 254))) {
            bool good = false;
            if (second >= 48 && second <= 57) {
              int third = next();
              if (third >= 129 && third <= 254) {
                int fourth = next();
                if (fourth >= 48 && fourth <= 57) {
                  cv = int((quint32(cv) << 16) | (third << 8) | fourth);
                  good = true;
                }
              }
            }
            error = !good;
          }
        }
      }
    } else if (first > 141) {
      int second = next();
      cv = (cv << 8) | second;
      if (first >= 161 && first <= 254)
        error = second < 161;
      else if (first == 142)
        error = second < 161;
      else if (first == 143) {
        int third = next();
        cv = int((quint32(cv) << 8) | quint32(third));
        error = third < 161;
      }
    }
    if (done)
      break;
    ++total;
    if (error)
      ++bad;
    else if (cv > 255) {
      ++doubles;
      if (std::find(common, common + commonSize, cv) != common + commonSize)
        ++hits;
    }
    if (bad >= 2 && 5 * bad >= doubles)
      return 0;
  }
  if (doubles <= 10 && !bad)
    return !doubles && total < 10 ? 0 : 10;
  if (doubles < 20 * bad)
    return 0;
  return std::min(100.0,
                  std::log(hits + 1) * 90 / std::log(double(doubles) / 4) + 10);
}
double iso2022(const QByteArray &b, const QVector<QByteArray> &seqs) {
  int hits = 0, misses = 0, shifts = 0;
  for (int i = 0; i < b.size(); ++i) {
    if (byte(b, i) == 27) {
      bool found = false;
      for (auto s : seqs)
        if (b.mid(i, s.size()) == s) {
          ++hits;
          i += s.size() - 1;
          found = true;
          break;
        }
      if (found)
        continue;
      ++misses;
    }
    if (byte(b, i) == 14 || byte(b, i) == 15)
      ++shifts;
  }
  if (!hits)
    return 0;
  return std::max(0., 100. * (hits - misses) / (hits + misses) -
                          (hits + shifts < 5 ? 10 * (5 - hits - shifts) : 0));
}
} // namespace
QString detectSubtitleCharset(const QByteArray &b) {
  QString result;
  double best = 0;
  auto offer = [&](double confidence, const QString &name) {
    if (confidence > best) {
      best = confidence;
      result = name;
    }
  };
  int valid = 0, invalid = 0;
  for (int i = 0; i < b.size(); ++i) {
    int v = byte(b, i), trail = 0;
    if (!(v & 128))
      continue;
    if ((v & 224) == 192)
      trail = 1;
    else if ((v & 240) == 224)
      trail = 2;
    else if ((v & 248) == 240)
      trail = 3;
    else {
      ++invalid;
      continue;
    }
    while (++i < b.size()) {
      if ((byte(b, i) & 192) != 128) {
        ++invalid;
        break;
      }
      if (--trail == 0) {
        ++valid;
        break;
      }
    }
  }
  bool bom = b.startsWith(QByteArray::fromHex("efbbbf"));
  offer(bom && !invalid               ? 100
        : bom && valid > 10 * invalid ? 80
        : valid > 3 && !invalid       ? 100
        : valid > 0 && !invalid       ? 80
        : !valid && !invalid          ? 15
        : valid > 10 * invalid        ? 25
                                      : 0,
        "UTF-8");
  for (bool le : {false, true}) {
    int confidence = 10, n = std::min(int(b.size()), 30);
    for (int i = 0; i < n - 1; i += 2) {
      int v = le ? (byte(b, i + 1) << 8) | byte(b, i)
                 : (byte(b, i) << 8) | byte(b, i + 1);
      if (!i && v == 65279) {
        confidence = 100;
        break;
      }
      if (v == 0)
        confidence -= 10;
      else if ((v >= 32 && v <= 255) || v == 10)
        confidence += 10;
      confidence = std::clamp(confidence, 0, 100);
      if (!confidence || confidence == 100)
        break;
    }
    if (n < 4 && confidence < 100)
      confidence = 0;
    offer(confidence, le ? "UTF-16LE" : "UTF-16BE");
  }
  for (bool le : {false, true}) {
    if (b.isEmpty())
      continue;
    int good = 0, bad = 0;
    bool bom = false;
    for (int i = 0; i < b.size(); i += 4) {
      quint32 u = 0;
      for (int j = 0; j < 4; ++j)
        u = (u << 8) | byte(b, i + (le ? 3 - j : j));
      qint32 c = qint32(u);
      if (i == 0 && c == 65279)
        bom = true;
      if (c < 0 || c >= 1114111 || (c >= 55296 && c <= 57343))
        ++bad;
      else
        ++good;
    }
    offer(bom && !bad              ? 100
          : bom && good > 10 * bad ? 80
          : good > 3 && !bad       ? 100
          : good > 0 && !bad       ? 80
          : good > 10 * bad        ? 25
                                   : 0,
          le ? "UTF-32LE" : "UTF-32BE");
  }
  offer(multi(b, 759, common759, std::size(common759)), "Shift_JIS");
  auto seq = [](std::initializer_list<const char *> values) {
    QVector<QByteArray> o;
    for (auto s : values)
      o.append(QByteArray::fromHex(s));
    return o;
  };
  offer(iso2022(b, seq({"1b242843", "1b242844", "1b2440", "1b2441", "1b2442",
                        "1b2640", "1b2842", "1b2848", "1b2849", "1b284a",
                        "1b2e41", "1b2e46"})),
        "ISO-2022-JP");
  offer(iso2022(b, seq({"1b242941", "1b242947", "1b242a48", "1b242945",
                        "1b242b49", "1b242b4a", "1b242b4b", "1b242b4c",
                        "1b242b4d", "1b4e", "1b4f"})),
        "ISO-2022-CN");
  offer(iso2022(b, seq({"1b242943"})), "ISO-2022-KR");
  offer(multi(b, 758, common758, std::size(common758)), "GB18030");
  offer(multi(b, 756, common756, std::size(common756)), "EUC-JP");
  offer(multi(b, 757, common757, std::size(common757)), "EUC-KR");
  offer(multi(b, 755, common755, std::size(common755)), "Big5");
  bool c1 = false;
  for (auto c : b)
    if (quint8(c) >= 128 && quint8(c) <= 159)
      c1 = true;
  for (auto t : singleTables)
    offer(single(b, t), c1 ? t.c1Name : t.name);
  return result;
}
} // namespace server1::media
