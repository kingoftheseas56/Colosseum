#include "reader2/mediaoverlay/SmilClock.h"
#include <QRegularExpression>
#include <QStringList>
#include <limits>

namespace MediaOverlay {
namespace {
// Decimal multiplication avoids binary floating rounding and qint64 overflow.
std::optional<qint64> scaled(QString number, int multiplier)
{
    const qsizetype dot = number.indexOf('.');
    const qsizetype fraction = dot < 0 ? 0 : number.size() - dot - 1;
    number.remove('.');
    QString product(number.size(), QChar('0'));
    int carry = 0;
    for (qsizetype i = number.size(); i-- > 0;) {
        const int n = number[i].digitValue() * multiplier + carry;
        product[i] = QChar('0' + n % 10);
        carry = n / 10;
    }
    if (carry) product.prepend(QString::number(carry));
    while (product.size() <= fraction) product.prepend('0');
    const qsizetype boundary = product.size() - fraction;
    bool ok = false;
    const qint64 whole = product.left(boundary).toLongLong(&ok);
    if (!ok) return std::nullopt;
    const bool round = fraction > 0 && product[boundary] >= QChar('5');
    if (round && whole == std::numeric_limits<qint64>::max()) return std::nullopt;
    return whole + (round ? 1 : 0);
}
bool add(qint64 &total, const std::optional<qint64> &value)
{
    if (!value || *value > std::numeric_limits<qint64>::max() - total) return false;
    total += *value;
    return true;
}
}
std::optional<qint64> SmilClock::parse(const QString &clock)
{
    const QString s = clock.trimmed();
    // Bound work on untrusted XML attribute values without integer conversion overflow.
    if (s.isEmpty() || s.size() > 1024) return std::nullopt;
    static const QRegularExpression count(QStringLiteral("^((?:[0-9]+(?:\\.[0-9]+)?|\\.[0-9]+))(h|min|s|ms)?$"));
    static const QRegularExpression form(QStringLiteral("^(?:[0-9]+:)?[0-9]+:[0-9]+(?:\\.[0-9]+)?$"));
    if (s.contains(':')) {
        if (!form.match(s).hasMatch()) return std::nullopt;
        const auto parts = s.split(':');
        const auto minute = parts[parts.size()-2].toULongLong();
        bool minuteOk = false, secondOk = false;
        parts[parts.size()-2].toULongLong(&minuteOk);
        const auto second = parts.last().section('.',0,0).toULongLong(&secondOk);
        if (!minuteOk || !secondOk || minute >= 60 || second >= 60) return std::nullopt;
        qint64 total = 0;
        if (parts.size() == 3 && !add(total, scaled(parts[0],3600000))) return std::nullopt;
        if (!add(total,scaled(parts[parts.size()-2],60000)) || !add(total,scaled(parts.last(),1000))) return std::nullopt;
        return total;
    }
    const auto match = count.match(s);
    if (!match.hasMatch()) return std::nullopt;
    const QString metric = match.captured(2);
    const int factor = metric == "h" ? 3600000 : metric == "min" ? 60000 : metric == "ms" ? 1 : 1000;
    return scaled(match.captured(1),factor);
}
}
