#include "reader2/mediaoverlay/EpubOverlayLoader.h"
#include "third_party/miniz/miniz.h"
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QXmlStreamReader>

namespace MediaOverlay {
QString errorName(Error error)
{
    switch (error) {
    case Error::None: return "none";
    case Error::FileMissing: return "file-missing";
    case Error::NotZip: return "not-zip";
    case Error::ContainerMissing: return "container-missing";
    case Error::ContainerMalformed: return "container-malformed";
    case Error::PackageMissing: return "package-missing";
    case Error::PackageMalformed: return "package-malformed";
    case Error::UnsafePath: return "unsafe-path";
    case Error::ResourceMissing: return "resource-missing";
    case Error::ResourceTooLarge: return "resource-too-large";
    case Error::ReadFailed: return "read-failed";
    case Error::WriteFailed: return "write-failed";
    }
    return "unknown-error";
}
namespace {
constexpr quint64 maxResource = 256ULL * 1024 * 1024;
constexpr quint64 maxXml = 8ULL * 1024 * 1024;
struct Archive {
    mz_zip_archive zip{};
    bool open = false;
    Archive() = default;
    Archive(const Archive &) = delete;
    Archive &operator=(const Archive &) = delete;
    ~Archive() { if (open) mz_zip_reader_end(&zip); }
    Error init(const QString &path) {
        if (!QFileInfo(path).isFile()) return Error::FileMissing;
        // The vendored Windows miniz implementation expects UTF-8, not the locale codec.
        const QByteArray utf8 = QDir::toNativeSeparators(path).toUtf8();
        open = mz_zip_reader_init_file(&zip,utf8.constData(),0);
        return open ? Error::None : Error::NotZip;
    }
    int locate(const QString &path) {
        const auto name = path.toUtf8();
        return mz_zip_reader_locate_file(&zip,name.constData(),nullptr,MZ_ZIP_FLAG_CASE_SENSITIVE);
    }
    std::optional<quint64> size(const QString &path) {
        const int index = locate(path);
        mz_zip_archive_file_stat stat{};
        if (index < 0 || !mz_zip_reader_file_stat(&zip,mz_uint(index),&stat) || stat.m_is_directory) return std::nullopt;
        return stat.m_uncomp_size;
    }
    ResourceResult read(const QString &path, quint64 limit = maxResource) {
        const auto safe = MediaOverlayPackage::containerPath(path);
        if (!safe) return {false,Error::UnsafePath,"Path escapes or is not a container resource",{}};
        const int index = locate(*safe);
        if (index < 0) return {false,Error::ResourceMissing,"Resource is absent: "+*safe,{}};
        const auto length = size(*safe);
        if (!length) return {false,Error::ReadFailed,"Cannot stat resource: "+*safe,{}};
        if (*length > limit) return {false,Error::ResourceTooLarge,"Resource exceeds read limit: "+*safe,{}};
        QByteArray data(qsizetype(*length),Qt::Uninitialized);
        if (!mz_zip_reader_extract_to_mem(&zip,mz_uint(index),data.data(),size_t(data.size()),0))
            return {false,Error::ReadFailed,"Resource extraction or CRC failed: "+*safe,{}};
        return {true,Error::None,{},data};
    }
};
std::optional<QString> rootfile(const QByteArray &bytes, QString &error)
{
    QXmlStreamReader xml(bytes);
    xml.setEntityExpansionLimit(4096);
    const QString ns = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:container");
    QString path;
    if (!xml.readNextStartElement() || xml.name() != u"container" || xml.namespaceUri() != ns)
        xml.raiseError(QStringLiteral("Expected OCF container root"));
    while (!xml.hasError() && xml.readNextStartElement()) {
        if (xml.namespaceUri() != ns || xml.name() != u"rootfiles") { xml.skipCurrentElement(); continue; }
        while (xml.readNextStartElement()) {
            if (xml.namespaceUri() == ns && xml.name() == u"rootfile" && path.isEmpty()
                && xml.attributes().value("media-type") == u"application/oebps-package+xml")
                path = xml.attributes().value("full-path").toString();
            xml.skipCurrentElement();
        }
    }
    while (!xml.atEnd()) xml.readNext();
    if (xml.hasError()) { error = QString("Line %1: %2").arg(xml.lineNumber()).arg(xml.errorString()); return std::nullopt; }
    auto resolved = MediaOverlayPackage::resolvePath("root",path);
    if (!resolved) error = QStringLiteral("No safe package rootfile");
    return resolved;
}
}
LoadResult EpubOverlayLoader::load(const QString &epubPath)
{
    LoadResult result;
    const auto fail = [&](Error error, const QString &message) {
        result.error = error; result.message = message; return result;
    };
    Archive archive;
    const Error opened = archive.init(epubPath);
    if (opened != Error::None) return fail(opened,errorName(opened)+": "+epubPath);
    const auto container = archive.read("META-INF/container.xml",maxXml);
    if (!container.ok) return fail(container.error == Error::ResourceMissing ? Error::ContainerMissing : container.error,container.message);
    QString containerError;
    const auto packagePath = rootfile(container.bytes,containerError);
    if (!packagePath) return fail(Error::ContainerMalformed,containerError);
    const auto packageBytes = archive.read(*packagePath,maxXml);
    if (!packageBytes.ok) return fail(packageBytes.error == Error::ResourceMissing ? Error::PackageMissing : packageBytes.error,packageBytes.message);
    const auto package = MediaOverlayPackage::parse(packageBytes.bytes,*packagePath);
    result.warnings = package.warnings;
    if (!package.ok) return fail(Error::PackageMalformed,QString("%1 line %2: %3").arg(*packagePath).arg(package.errorLine).arg(package.error));
    result.hasOverlays = !package.overlays.isEmpty();
    result.durationMs = package.durationMs;
    result.overlayDurations = package.overlayDurations;
    result.activeClass = package.activeClass;
    result.playbackActiveClass = package.playbackActiveClass;
    QMap<QString,DocumentResult> documents;
    QSet<QString> visited;
    for (const auto &path : package.overlays) {
        if (visited.contains(path)) continue;
        visited.insert(path);
        const auto bytes = archive.read(path,maxXml);
        if (!bytes.ok) { result.warnings.append({"overlay-missing",bytes.message,path,0,0}); continue; }
        auto doc = MediaOverlayDocument::parse(bytes.bytes);
        for (auto warning : doc.warnings) { warning.source = path; result.warnings.append(warning); }
        if (!doc.ok) { result.warnings.append({"overlay-malformed",doc.error,path,doc.errorLine,0}); continue; }
        documents.insert(path,doc);
    }
    QSet<QString> audioSeen;
    for (const auto &spine : package.spine) {
        const auto doc = documents.constFind(spine.overlayPath);
        if (doc == documents.cend()) continue;
        qint64 position = 0;
        for (const auto &entry : doc->entries) {
            ++position;
            const auto text = MediaOverlayPackage::resolvePath(spine.overlayPath,entry.textPath);
            const auto audio = entry.textOnly ? std::optional<QString>(QString()) : MediaOverlayPackage::resolvePath(spine.overlayPath,entry.audioPath);
            if (!text || !audio) { result.warnings.append({"unsafe-overlay-path","Rejected text or audio reference",spine.overlayPath,0,position}); continue; }
            // A shared overlay can contain several chapters. Emit only this spine document's cues.
            if (*text != spine.path) continue;
            Cue cue;
            cue.textPath = *text;
            cue.fragmentId = QUrl::fromPercentEncoding(entry.fragmentId.toUtf8());
            cue.audioPath = *audio;
            cue.beginMs = entry.beginMs;
            cue.endMs = entry.endMs;
            cue.semanticTypes = entry.types;
            for (const auto &seq : entry.sequences) {
                cue.structurePath.append(seq.id);
                cue.semanticTypes.append(seq.types);
            }
            cue.semanticTypes.removeDuplicates();
            if (!archive.size(*text)) result.warnings.append({"resource-missing","Text resource is absent: "+*text,spine.overlayPath,0,position});
            if (!audio->isEmpty()) {
                const auto size = archive.size(*audio);
                if (!size) result.warnings.append({"resource-missing","Audio resource is absent: "+*audio,spine.overlayPath,0,position});
                else if (!audioSeen.contains(*audio)) { audioSeen.insert(*audio); result.audioResources.append({*audio,*size}); }
            }
            if (!result.timeline.append(cue)) result.warnings.append({"invalid-cue","Invalid timeline range",spine.overlayPath,0,position});
        }
    }
    result.ok = true;
    return result;
}
ResourceResult EpubOverlayLoader::readResource(const QString &epubPath, const QString &path)
{
    if (!MediaOverlayPackage::containerPath(path)) return {false,Error::UnsafePath,"Unsafe container path",{}};
    Archive archive;
    const Error error = archive.init(epubPath);
    if (error != Error::None) return {false,error,errorName(error),{}};
    return archive.read(path);
}
ResourceResult EpubOverlayLoader::extractResource(const QString &epubPath, const QString &path, const QString &destination)
{
    auto resource = readResource(epubPath,path);
    if (!resource.ok) return resource;
    // Do not let an extraction replace its own source archive.
    if (QFileInfo(epubPath).absoluteFilePath() == QFileInfo(destination).absoluteFilePath()
        || (QFileInfo(destination).exists() && QFileInfo(epubPath).canonicalFilePath() == QFileInfo(destination).canonicalFilePath()))
        return {false,Error::WriteFailed,"Destination is the source archive",{}};
    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly) || file.write(resource.bytes) != resource.bytes.size() || !file.commit())
        return {false,Error::WriteFailed,file.errorString(),{}};
    resource.bytes.clear();
    return resource;
}
}
