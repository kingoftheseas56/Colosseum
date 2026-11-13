#include "StreamTypes.h"
#include <cmath>
#include <limits>

namespace colosseum::stream {
namespace {
template <class T> std::optional<T> nonnegativeInteger(const QJsonValue &value) {
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    // JSON numbers above the exact integer range cannot identify byte sizes reliably.
    if (!std::isfinite(number) || number < 0 || std::floor(number) != number ||
        number > 9007199254740991.0 ||
        static_cast<long double>(number) > std::numeric_limits<T>::max())
        return std::nullopt;
    return static_cast<T>(number);
}
} // namespace
Stream stream_from_json(const QJsonObject &row, int index, const QString &addonName,
                        int addonPriority) {
    Stream result;
    result.raw = row;
    result.input_index = index;
    result.name = row.value("name").toString();
    result.title = row.value("title").toString();
    if (result.title.isEmpty())
        result.title = row.value("release").toString();
    result.description = row.value("description").toString();
    result.info_hash = row.value("infoHash").toString();
    result.url = row.value("url").toString();
    if (result.info_hash.startsWith("url:")) {
        if (result.url.isEmpty())
            result.url = result.info_hash.mid(4);
        result.info_hash.clear();
    }
    result.yt_id = row.value("ytId").toString();
    result.external_url = row.value("externalUrl").toString();
    result.addon_id = row.value("addonId").toString();
    result.addon_name = addonName.isEmpty() ? row.value("addonName").toString() : addonName;
    result.addon_priority = addonPriority >= 0
                                ? std::optional<int>(addonPriority)
                                : nonnegativeInteger<int>(row.value("addonPriority"));
    result.addon_return_idx = nonnegativeInteger<int>(row.value("addonReturnIdx"));
    result.behavior_hints = row.value("behaviorHints").toObject();
    // Accept AddonClient/Torrentio's normalized rows as well as raw addon JSON.
    for (const auto *field : {"filename", "bingeGroup"})
        if (result.behavior_hints.value(field).toString().isEmpty() && row.value(field).isString())
            result.behavior_hints.insert(field, row.value(field));
    result.size = nonnegativeInteger<quint64>(row.value("size"));
    if (!result.size)
        result.size = nonnegativeInteger<quint64>(row.value("sizeBytes"));
    if (!result.size)
        result.size = nonnegativeInteger<quint64>(result.behavior_hints.value("videoSize"));
    result.seeders = nonnegativeInteger<quint32>(row.value("seeders"));
    if (row.value("size").isString())
        result.description += "\n" + row.value("size").toString();
    return result;
}
} // namespace colosseum::stream
