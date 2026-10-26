#pragma once
#include <QString>
#include <QStringList>
#include <optional>
namespace server1::http {
struct FileRegexResult {bool valid=false;std::optional<bool> matches;QString error;};
// M172 slash/flag parsing followed by M80's bounded ECMAScript matching.
// null matches means an interrupted match, which the source ignores.
FileRegexResult matchFilePattern(const QString &subject,const QString &expression);
std::optional<std::size_t> selectFilePattern(const QStringList &names,const QStringList &patterns);
}
