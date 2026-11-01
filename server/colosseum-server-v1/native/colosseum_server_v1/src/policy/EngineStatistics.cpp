#include "server1/policy/EngineStatistics.h"
#include <cmath>
#include <limits>
namespace server1::policy {
Value projectEngineStatistics(const EngineStatisticsSnapshot &s, const Value &index)
{
    Value files=Value::null();
    if(s.files) {
        Value::Array entries;
        for(const auto &file:*s.files) entries.push_back(Value::object({
            {"path",Value::string(file.path)},{"name",Value::string(file.name)},
            {"length",Value::number(file.length)},{"offset",Value::number(file.offset)}}));
        files=Value::array(std::move(entries));
    }
    Value::Object result{
        {"infoHash",Value::string(s.infoHash)},
        {"name",s.name ? Value::string(*s.name) : Value::null()},
        {"peers",Value::number(s.peers)},{"unchoked",Value::number(s.unchoked)},
        {"queued",Value::number(s.queued)},{"unique",Value::number(s.unique)},
        {"connectionTries",Value::number(s.connectionTries)},
        {"swarmPaused",Value::boolean(s.swarmPaused)},
        {"swarmConnections",Value::number(s.swarmConnections)},
        {"swarmSize",s.swarmSize},{"selections",s.selections},
        {"wires",index.isMissing() ? s.wires : Value::null()},
        {"files",std::move(files)}, {"downloaded",Value::number(s.downloaded)},
        {"uploaded",Value::number(s.uploaded)},
        {"downloadSpeed",Value::number(s.downloadSpeed)},
        // Deliberate M172 oddity: BOTH fields call swarm.downloadSpeed().
        {"uploadSpeed",Value::number(s.downloadSpeed)},
        {"sources",s.sources},{"peerSearchRunning",s.peerSearchRunning},{"opts",s.options}
    };
    // JavaScript array property lookup is not numeric coercion. "01", null,
    // false and an empty string pass isNaN but do not name file zero/one.
    const auto number=jsNumber(index);
    bool canonical=index.kind()==Value::Kind::Number;
    if(index.kind()==Value::Kind::String && std::isfinite(number) && number>=0
       && number<=static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
        canonical=index.asString()==std::to_string(static_cast<std::uint32_t>(number));
    if(canonical && s.files && std::isfinite(number) && number>=0
       && std::floor(number)==number && number<static_cast<double>(s.files->size())) {
        const auto &file=(*s.files)[static_cast<std::size_t>(number)];
        const auto start=jsToInt32(Value::number(static_cast<double>(file.offset)/s.pieceLength));
        const auto end=jsToInt32(Value::number((static_cast<double>(file.offset)+file.length-1)/s.pieceLength));
        std::uint64_t available=0;
        for(const auto piece:s.availablePieces)
            if(static_cast<double>(piece)>=start && static_cast<double>(piece)<=end) ++available;
        const auto count=std::ceil(static_cast<double>(file.length)/s.pieceLength);
        result.emplace_back("streamLen",Value::number(file.length));
        result.emplace_back("streamName",Value::string(file.name));
        result.emplace_back("streamProgress",Value::number(available/count));
    }
    return Value::object(std::move(result));
}
}
