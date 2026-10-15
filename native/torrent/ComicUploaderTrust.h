#pragma once

#include <QString>
#include <QStringList>

// Uploader-trust reader for the Tankorent Comic volume-mode feature. Reads a
// bounded release tag ("[Nem]", "(Nem)", "(- Nem -)", or a trailing "- Nem")
// out of a torrent title and grades it against the bundled trust table.
// Trust influences ranking only; it never bypasses identity safety. No
// network, no Qt GUI — the trust table itself loads from the bundled Qt
// resource, not a disk path.
namespace ComicUploaderTrust {

struct UploaderTrust {
    int tier = 99;   // 1 = tier1 (trusted), 2 = tier2, 99 = unknown, -1 = blocked
    QString name;
};

struct TrustTable {
    QStringList tier1;
    QStringList tier2;
    QStringList blocked;
};

// Reads :/tankorent/comics_uploader_trust.json (Qt resource). Missing
// resource, malformed JSON, or an unrecognized `version` all degrade to an
// empty table (no automatic trust) rather than throwing or crashing.
TrustTable load();

// Extracts an uploader ONLY from a bounded release-tag position — a bare
// substring occurrence elsewhere in the title (e.g. "nem" inside "Nemesis")
// is not uploader evidence. Matching a tag against `table` is
// case-insensitive and whole-tag.
UploaderTrust taggedUploader(const QString& releaseTitle, const TrustTable& table);

} // namespace ComicUploaderTrust
