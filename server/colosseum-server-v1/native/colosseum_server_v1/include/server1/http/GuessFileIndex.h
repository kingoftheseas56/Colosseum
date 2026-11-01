#pragma once

#include "server1/policy/Value.h"
#include <string>

namespace server1::http {

// M304, with the default options used by M663. This is the complete metadata
// path, including the title/type/date gates that remove season and episode.
// Strings use the same UTF-8 representation as policy::Value.
[[nodiscard]] policy::Value parseVideoName(const std::string &filePath);

// M663. A number-valued result: the original files index, or -1. Invalid file
// entries/path types throw std::invalid_argument, as the source throws TypeError.
// The value model covers ordinary data values, not JavaScript getters/functions.
[[nodiscard]] policy::Value guessFileIndex(const policy::Value &files,
                                         const policy::Value &seriesInfo);

} // namespace server1::http
