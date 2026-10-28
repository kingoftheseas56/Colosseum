#include "server1/policy/SourcePeerAddress.h"
#include "server1/policy/Value.h"
namespace server1::policy {
std::optional<SourcePeerAddress> parseSourcePeerAddress(const std::string &raw)
{
    const auto colon=raw.find(':');
    if(colon==std::string::npos)return {};
    const auto end=raw.find(':',colon+1);
    const auto port=raw.substr(colon+1,end==std::string::npos ? end : end-colon-1);
    const double number=jsNumber(Value::string(port));
    if(!(number>0 && number<65535))return {};
    return SourcePeerAddress{raw,raw.substr(0,colon),port,number};
}
}
