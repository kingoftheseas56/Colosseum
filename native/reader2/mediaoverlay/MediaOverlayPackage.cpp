#include "reader2/mediaoverlay/MediaOverlayPackage.h"
#include "reader2/mediaoverlay/SmilClock.h"
#include <QUrl>
#include <QStringDecoder>
#include <QXmlStreamReader>

namespace MediaOverlay {
std::optional<QString> MediaOverlayPackage::containerPath(const QString &path)
{
    if (path.isEmpty() || path.startsWith('/') || path.contains('\\') || path.contains(':') || path.contains('?') || path.contains('#')) return std::nullopt;
    for (const QChar c : path) if (c.unicode() < 32 || c.unicode() == 127) return std::nullopt;
    QStringList parts;
    for (const auto &part : path.split('/')) {
        if (part.isEmpty() || part == ".") continue;
        if (part == "..") { if (parts.isEmpty()) return std::nullopt; parts.removeLast(); }
        else parts.append(part);
    }
    if (parts.isEmpty()) return std::nullopt;
    return parts.join('/');
}
std::optional<QString> MediaOverlayPackage::resolvePath(const QString &baseFile, const QString &href)
{
    if (href.isEmpty() || href.startsWith('/') || href.contains('?') || href.contains('#')) return std::nullopt;
    // QUrl's forgiving decoder would preserve malformed escapes. Reject them first.
    for (qsizetype i = 0; i < href.size(); ++i) {
        if (href[i] != '%') continue;
        const auto hex = [](QChar c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); };
        if (i+2 >= href.size() || !hex(href[i+1]) || !hex(href[i+2])) return std::nullopt;
        i += 2;
    }
    QStringDecoder decode(QStringDecoder::Utf8);
    const QString decoded = decode(QByteArray::fromPercentEncoding(href.toUtf8()));
    if (decode.hasError() || decoded.startsWith('/')) return std::nullopt;
    const auto base = containerPath(baseFile);
    if (!base) return std::nullopt;
    const QString folder = base->left(base->lastIndexOf('/')+1);
    return containerPath(folder + decoded);
}
PackageResult MediaOverlayPackage::parse(const QByteArray &bytes, const QString &packagePath)
{
    PackageResult result;
    QXmlStreamReader xml(bytes);
    xml.setEntityExpansionLimit(4096);
    const QString ns = QStringLiteral("http://www.idpf.org/2007/opf");
    const auto warn = [&](const QString &code, const QString &message, qint64 position = 0) {
        result.warnings.append({code,message,packagePath,xml.lineNumber(),position});
    };
    if (!containerPath(packagePath)) xml.raiseError(QStringLiteral("Invalid package container path"));
    if (!xml.hasError() && (!xml.readNextStartElement() || xml.name() != u"package" || xml.namespaceUri() != ns))
        xml.raiseError(QStringLiteral("Expected OPF namespace package root"));
    QStringList spineIds;
    while (!xml.hasError() && xml.readNextStartElement()) {
        if (xml.namespaceUri() != ns) { xml.skipCurrentElement(); continue; }
        if (xml.name() == u"manifest") {
            qint64 position = 0;
            while (xml.readNextStartElement()) {
                if (xml.namespaceUri() == ns && xml.name() == u"item") {
                    ++position;
                    const auto a = xml.attributes();
                    const QString id = a.value("id").toString();
                    const auto path = resolvePath(packagePath,a.value("href").toString());
                    if (!path) warn("unsafe-path","Rejected manifest href: "+a.value("href").toString(),position);
                    else if (id.isEmpty() || result.manifest.contains(id)) warn("invalid-manifest-id","Empty or duplicate manifest id: "+id,position);
                    else result.manifest.insert(id,{id,*path,a.value("media-type").toString(),a.value("media-overlay").toString()});
                }
                xml.skipCurrentElement();
            }
        } else if (xml.name() == u"spine") {
            while (xml.readNextStartElement()) {
                if (xml.namespaceUri() == ns && xml.name() == u"itemref") spineIds.append(xml.attributes().value("idref").toString());
                xml.skipCurrentElement();
            }
        } else if (xml.name() == u"metadata") {
            while (xml.readNextStartElement()) {
                if (xml.namespaceUri() != ns || xml.name() != u"meta") { xml.skipCurrentElement(); continue; }
                const QString property = xml.attributes().value("property").toString();
                const QString refines = xml.attributes().value("refines").toString();
                const QString value = xml.readElementText().trimmed();
                if (property == "media:duration") {
                    const auto duration = SmilClock::parse(value);
                    if (!duration) warn("invalid-duration","Invalid media duration: "+value);
                    else if (refines.isEmpty()) result.durationMs = duration;
                    else if (refines.startsWith('#')) result.overlayDurations.insert(refines.mid(1),*duration);
                    else warn("invalid-refinement","Duration refines must identify an overlay id");
                } else if (property == "media:active-class" && refines.isEmpty()) result.activeClass = value;
                else if (property == "media:playback-active-class" && refines.isEmpty()) result.playbackActiveClass = value;
            }
        } else xml.skipCurrentElement();
    }
    while (!xml.atEnd()) xml.readNext();
    if (xml.hasError()) {
        result.error = xml.errorString(); result.errorLine = xml.lineNumber();
        result.manifest.clear(); result.spine.clear(); result.overlayDurations.clear();
        result.durationMs.reset(); result.activeClass.clear(); result.playbackActiveClass.clear();
        return result;
    }
    for (auto it = result.manifest.cbegin(); it != result.manifest.cend(); ++it) {
        if (it->overlayId.isEmpty()) continue;
        const auto overlay = result.manifest.constFind(it->overlayId);
        if (overlay == result.manifest.cend()) warn("missing-overlay","Unknown overlay id: "+it->overlayId);
        else if (overlay->mediaType != "application/smil+xml") warn("wrong-overlay-type","Not a SMIL overlay: "+it->overlayId);
        else result.overlays.insert(it->path,overlay->path);
    }
    for (const auto &id : spineIds) {
        const auto item = result.manifest.constFind(id);
        if (item == result.manifest.cend()) warn("missing-spine-item","Unknown spine id: "+id);
        else result.spine.append({id,item->path,result.overlays.value(item->path)});
    }
    result.ok = true;
    return result;
}
}
