#include "server1/http/TorrentRange.h"
#include "server1/policy/Value.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
namespace server1::http {
std::optional<TorrentRange> firstTorrentRange(std::size_t length,std::string_view header)
{
    const auto equals=header.find('=');
    if(equals==std::string_view::npos)return {};
    auto remaining=header.substr(equals+1);
    while(true){
        const auto comma=remaining.find(',');
        const auto part=remaining.substr(0,comma);
        const auto dash=part.find('-');
        auto parse=[](std::string_view value){return policy::jsParseInt(policy::Value::string(std::string(value)),10);};
        double start=parse(part.substr(0,dash));
        double end=std::numeric_limits<double>::quiet_NaN();
        if(dash!=std::string_view::npos){const auto tail=part.substr(dash+1);end=parse(tail.substr(0,tail.find('-')));}
        const double size=static_cast<double>(length);
        if(std::isnan(start)){start=size-end;end=size-1;}
        else if(std::isnan(end))end=size-1;
        if(end>size-1)end=size-1;
        if(std::isfinite(start) && std::isfinite(end) && start>=0 && start<=end
            && end<static_cast<long double>(std::numeric_limits<std::size_t>::max()))
            return TorrentRange{static_cast<std::size_t>(start),static_cast<std::size_t>(end)};
        if(comma==std::string_view::npos)break;
        remaining.remove_prefix(comma+1);
    }
    return {};
}
}
