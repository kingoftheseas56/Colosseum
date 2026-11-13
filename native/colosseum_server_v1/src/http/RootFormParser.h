#pragma once
#include "server1/policy/Value.h"
#include <QByteArray>
#include <QRegularExpression>
#include <QString>
#include <QUrl>
#include <algorithm>

namespace server1::http::detail {
using policy::Value;
inline bool formObject(const Value &v) { return v.kind()==Value::Kind::Object || v.kind()==Value::Kind::Array; }
inline Value::Object formEntries(const Value &v) {
    if(v.kind()==Value::Kind::Object)return v.asObject();
    Value::Object result;
    if(v.kind()==Value::Kind::Array)for(size_t i=0;i<v.asArray().size();++i)if(!v.asArray()[i].isMissing())result.emplace_back(std::to_string(i),v.asArray()[i]);
    return result;
}
// M285 qs.merge: retain sparse array indexes until the final compact pass.
inline Value formMerge(Value target,const Value &source) {
    if(source.isMissing())return target;
    if(!formObject(source)) {
        if(target.kind()==Value::Kind::Array){auto a=target.asArray();a.push_back(source);return Value::array(std::move(a));}
        if(!formObject(target))return Value::array({target,source});
        auto o=target.asObject();
        if(source.kind()==Value::Kind::String && source.asString()!="__proto__") {
            auto i=std::find_if(o.begin(),o.end(),[&](const auto &p){return p.first==source.asString();});
            if(i==o.end())o.emplace_back(source.asString(),Value::boolean(true));else i->second=Value::boolean(true);
        }
        return Value::object(std::move(o));
    }
    if(!formObject(target)) {
        Value::Array a{target};
        if(source.kind()==Value::Kind::Array)a.insert(a.end(),source.asArray().begin(),source.asArray().end());else a.push_back(source);
        return Value::array(std::move(a));
    }
    if(target.kind()==Value::Kind::Array && source.kind()==Value::Kind::Array) {
        auto a=target.asArray();const auto &b=source.asArray();
        for(size_t i=0;i<b.size();++i) {
            if(b[i].isMissing())continue;
            if(i>=a.size())a.resize(i+1);
            if(a[i].isMissing())a[i]=b[i];
            else if(formObject(a[i]) && formObject(b[i]))a[i]=formMerge(a[i],b[i]);
            else a.push_back(b[i]);
        }
        return Value::array(std::move(a));
    }
    auto o=formEntries(target);
    for(const auto &[key,v]:formEntries(source)) {
        if(key=="__proto__")continue;
        auto i=std::find_if(o.begin(),o.end(),[&](const auto &p){return p.first==key;});
        if(i==o.end())o.emplace_back(key,v);else i->second=formMerge(i->second,v);
    }
    return Value::object(std::move(o));
}
inline Value formCompact(const Value &v) {
    if(v.kind()==Value::Kind::Array) {Value::Array a;for(const auto &x:v.asArray())if(!x.isMissing())a.push_back(formCompact(x));return Value::array(std::move(a));}
    if(v.kind()==Value::Kind::Object) {auto o=v.asObject();for(auto &p:o)p.second=formCompact(p.second);return Value::object(std::move(o));}
    return v;
}
inline Value parseRootForm(const QByteArray &body) {
    const auto parts=body.split('&');
    Value::Object fields;
    auto decode=[](QByteArray input){input.replace('+',' ');return QUrl::fromPercentEncoding(input).toStdString();};
    for(const auto &part:parts) {
        auto position=part.indexOf("]=");position=position<0?part.indexOf('='):position+1;
        const auto key=decode(position<0?part:part.left(position));
        if(key.empty() || key=="__proto__")continue;
        const auto v=Value::string(position<0?"":decode(part.mid(position+1)));
        auto i=std::find_if(fields.begin(),fields.end(),[&](const auto &p){return p.first==key;});
        if(i==fields.end())fields.emplace_back(key,v);
        else {auto a=i->second.kind()==Value::Kind::Array?i->second.asArray():Value::Array{i->second};a.push_back(v);i->second=Value::array(std::move(a));}
    }
    Value result=Value::object({});
    const auto arrayLimit=std::max<qsizetype>(100,parts.size()-1);
    const QRegularExpression bracket("\\[[^\\[\\]]*\\]");
    for(const auto &[key,v]:fields) {
        const auto text=QString::fromStdString(key);const auto first=bracket.match(text);
        QStringList chain;
        const auto parent=first.hasMatch()?text.left(first.capturedStart()):text;
        if(!parent.isEmpty())chain.push_back(parent);
        auto matches=bracket.globalMatch(text);while(matches.hasNext())chain.push_back(matches.next().captured());
        auto leaf=v;
        for(auto i=chain.crbegin();i!=chain.crend();++i) {
            if(*i=="[]")leaf=leaf.kind()==Value::Kind::Array?leaf:Value::array({leaf});
            else {
                const auto clean=i->startsWith('[')&&i->endsWith(']')?i->mid(1,i->size()-2):*i;
                if(clean=="__proto__"){leaf=Value::object({});continue;}
                bool valid=false;const auto n=clean.toLongLong(&valid);
                if(valid && clean!=*i && QString::number(n)==clean && n>=0 && n<=arrayLimit) {
                    Value::Array a(static_cast<size_t>(n)+1);a[n]=leaf;leaf=Value::array(std::move(a));
                } else leaf=Value::object({{clean.toStdString(),leaf}});
            }
        }
        result=formMerge(std::move(result),leaf);
    }
    return formCompact(result);
}
}
