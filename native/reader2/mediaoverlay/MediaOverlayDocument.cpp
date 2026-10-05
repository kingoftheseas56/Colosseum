#include "reader2/mediaoverlay/MediaOverlayDocument.h"
#include "reader2/mediaoverlay/SmilClock.h"
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace MediaOverlay {
namespace {
const QString smil = QStringLiteral("http://www.w3.org/ns/SMIL");
const QString ops = QStringLiteral("http://www.idpf.org/2007/ops");
QStringList types(const QXmlStreamAttributes &a)
{
    return a.value(ops,QStringLiteral("type")).toString().split(QRegularExpression(QStringLiteral("\\s+")),Qt::SkipEmptyParts);
}
void readPar(QXmlStreamReader &xml, const QVector<Sequence> &chain, qint64 position, DocumentResult &out)
{
    OverlayEntry entry;
    entry.id = xml.attributes().value("id").toString();
    entry.types = types(xml.attributes());
    entry.sequences = chain;
    const qint64 line = xml.lineNumber();
    bool text = false, audio = false, invalid = false;
    while (xml.readNextStartElement()) {
        if (xml.namespaceUri() != smil) { xml.skipCurrentElement(); continue; }
        const auto attrs = xml.attributes();
        if (xml.name() == u"text") {
            if (text) invalid = true;
            text = true;
            const QString src = attrs.value("src").toString();
            const qsizetype hash = src.indexOf('#');
            entry.textPath = hash < 0 ? src : src.left(hash);
            entry.fragmentId = hash < 0 ? QString() : src.mid(hash+1);
            if (src.isEmpty()) text = false;
        } else if (xml.name() == u"audio") {
            if (audio) invalid = true;
            audio = true;
            entry.textOnly = false;
            entry.audioPath = attrs.value("src").toString();
            const auto begin = attrs.hasAttribute("clipBegin") ? SmilClock::parse(attrs.value("clipBegin").toString()) : std::optional<qint64>(0);
            if (!begin || entry.audioPath.isEmpty()) invalid = true;
            else entry.beginMs = *begin;
            if (attrs.hasAttribute("clipEnd")) {
                entry.endMs = SmilClock::parse(attrs.value("clipEnd").toString());
                if (!entry.endMs || *entry.endMs <= entry.beginMs) invalid = true;
            }
        }
        xml.skipCurrentElement();
    }
    if (!text || invalid) {
        out.warnings.append({!text ? QStringLiteral("missing-text") : QStringLiteral("invalid-clip"),
            !text ? QStringLiteral("par has no SMIL text reference") : QStringLiteral("Invalid audio source, duplicate child or clip range"),{},line,position});
    } else out.entries.append(entry);
}
}
DocumentResult MediaOverlayDocument::parse(const QByteArray &bytes)
{
    DocumentResult result;
    QXmlStreamReader xml(bytes);
    xml.setEntityExpansionLimit(4096);
    if (!xml.readNextStartElement() || xml.name() != u"smil" || xml.namespaceUri() != smil)
        xml.raiseError(QStringLiteral("Expected SMIL namespace smil root"));
    bool body = false;
    qint64 position = 0;
    while (!xml.hasError() && xml.readNextStartElement()) {
        if (xml.name() != u"body" || xml.namespaceUri() != smil) { xml.skipCurrentElement(); continue; }
        if (body) { xml.raiseError(QStringLiteral("Duplicate SMIL body")); break; }
        body = true;
        QVector<Sequence> chain;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                if (xml.namespaceUri() != smil) { xml.skipCurrentElement(); continue; }
                if (xml.name() == u"seq") {
                    if (chain.size() >= 256) { xml.raiseError(QStringLiteral("Sequence nesting limit exceeded")); break; }
                    const auto a = xml.attributes();
                    chain.append({a.value("id").toString(),types(a),a.value(ops,QStringLiteral("textref")).toString()});
                } else if (xml.name() == u"par") readPar(xml,chain,++position,result);
                else xml.skipCurrentElement();
            } else if (xml.isEndElement()) {
                if (xml.name() == u"body") break;
                if (xml.name() == u"seq" && !chain.isEmpty()) chain.removeLast();
            }
        }
    }
    if (!body && !xml.hasError()) xml.raiseError(QStringLiteral("Missing SMIL body"));
    while (!xml.atEnd()) xml.readNext();
    if (xml.hasError()) {
        result.entries.clear();
        result.error = xml.errorString();
        result.errorLine = xml.lineNumber();
        return result;
    }
    result.ok = true;
    return result;
}
}
