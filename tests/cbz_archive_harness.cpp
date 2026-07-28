#include "engine/CbzArchive.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

using namespace MangaTankoban;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly)
        && f.write(bytes) == bytes.size();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir temp;
    require(temp.isValid(), "temporary archive root created");
    const QString pagesDir = temp.path() + QStringLiteral("/pages");
    require(QDir().mkpath(pagesDir), "page source directory created");

    const QByteArray page2("\x89PNG-page-two", 13);
    const QByteArray page10("\xFF\xD8\xFF-page-ten", 12);
    require(writeBytes(pagesDir + QStringLiteral("/page_2.png"), page2),
            "page_2 fixture written");
    require(writeBytes(pagesDir + QStringLiteral("/page_10.jpg"), page10),
            "page_10 fixture written");

    const QString archive = temp.path() + QStringLiteral("/volume.cbz");
    QString error;
    require(CbzArchive::writeImagesAtomic(
                archive, pagesDir,
                {QStringLiteral("page_2.png"), QStringLiteral("page_10.jpg")}, &error),
            "atomic CBZ write succeeds");
    require(QFileInfo::exists(archive), "final CBZ exists");
    require(!QFileInfo::exists(archive + QStringLiteral(".part")),
            "successful write leaves no part file");

    const QVector<CbzPageEntry> entries = CbzArchive::imageEntries(archive, &error);
    require(entries.size() == 2, "two image entries listed");
    require(entries[0].name == QStringLiteral("page_2.png")
                && entries[1].name == QStringLiteral("page_10.jpg"),
            "image entries are naturally ordered");
    require(CbzArchive::readEntry(archive, entries[0].name, &error) == page2,
            "first entry bytes round-trip exactly");
    require(CbzArchive::readEntry(archive, entries[1].name, &error) == page10,
            "second entry bytes round-trip exactly");

    // A failed replacement must not clobber the already-valid archive.
    const QByteArray before = CbzArchive::readEntry(archive, QStringLiteral("page_2.png"), &error);
    require(!CbzArchive::writeImagesAtomic(
                archive, pagesDir,
                {QStringLiteral("page_2.png"), QStringLiteral("missing.png")}, &error),
            "missing source page rejects replacement");
    require(CbzArchive::readEntry(archive, QStringLiteral("page_2.png"), &error) == before,
            "failed replacement preserves prior archive");
    require(!QFileInfo::exists(archive + QStringLiteral(".part")),
            "failed replacement removes part file");

    std::cout << "PASS: CBZ archive contract\n";
    return 0;
}

