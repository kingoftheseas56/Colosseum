#include "server1/policy/SourcePeerAddress.h"
#include "server1/policy/Value.h"
#include <iostream>
using namespace server1::policy;
int main() {
    for(const auto &raw:{"127.0.0.1:1","127.0.0.1:01","localhost:80",":80","999.1.1.1:5",
        "127.0.0.1:65535","127.0.0.1:65534","127.0.0.1:0","host:1.5","host:0x50",
        "host:1e2","host: 01 ","[::1]:80","::1:80","fe80:123:tail","host:80:extra",
        "host:5x","host:","bad","host:-1","host:Infinity","host:0b10","host:0o10"}) {
        const auto parsed=parseSourcePeerAddress(raw);
        Value::Object out{{"accepted",Value::boolean(bool(parsed))}};
        if(parsed){out.emplace_back("raw",Value::string(parsed->raw));out.emplace_back("host",Value::string(parsed->host));
            out.emplace_back("portText",Value::string(parsed->portText));out.emplace_back("portNumber",Value::number(parsed->portNumber));}
        std::cout<<jsonStringify(Value::object(std::move(out)))<<'\n';
    }
}
