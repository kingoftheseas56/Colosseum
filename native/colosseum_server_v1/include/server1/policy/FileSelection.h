#pragma once

#include "server1/policy/Value.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// K12 guessed-file selection: M304 video-name parsing, M663 GuessFileIdx, and
// the M172 fileMustInclude alternatives evaluated through M80
// safeStatelessRegex.
namespace server1::policy {

// M304(filePath, options). The result object keeps the source's property
// order, including deletions and re-insertions. options may carry strict,
// fromInside, fileLength and hints.imdb_id.
[[nodiscard]] Value parseVideoName(std::string_view filePath,
                                   const Value &options = Value::object({}));

// M663(files, seriesInfo): -1 or the chosen index. files is the torrent file
// array (path, length); seriesInfo is any JSON value.
[[nodiscard]] double guessFileIdx(const Value &files, const Value &seriesInfo);

// A JavaScript RegExp as M172 builds it, evaluated as M80 does
// (!!str.match(re)). A pattern this engine cannot evaluate, or an evaluation
// that exhausts the regex engine, is treated as M80 treats a timeout: no match.
class JsRegExp final {
public:
    // new RegExp(pattern, flags); nullopt where the source constructor throws.
    [[nodiscard]] static std::optional<JsRegExp> compile(std::string pattern,
                                                        std::string flags = {});
    [[nodiscard]] bool test(std::string_view text) const;
    [[nodiscard]] const std::string &source() const noexcept { return pattern_; }
    [[nodiscard]] const std::string &flags() const noexcept { return flags_; }
    [[nodiscard]] bool nativelyEvaluable() const noexcept { return evaluable_; }

private:
    JsRegExp() = default;
    std::string pattern_;
    std::string flags_;
    bool evaluable_ = false;
    struct Compiled;
    std::shared_ptr<const Compiled> compiled_;
};

// The M172 fileMustInclude search over torrent files (by file.name). index is
// the first file any alternative matches. error is set where the source throws
// instead of answering (a non-string truthy element, or a string alternative
// that is not a valid pattern); the source never reaches its response then.
struct FileMustIncludeResult final {
    std::optional<std::size_t> index;
    std::optional<std::string> error;
};
[[nodiscard]] FileMustIncludeResult matchFileMustInclude(const Value &alternatives,
                                                         const Value &files);

// The /:infoHash/create selection: fileMustInclude first, then GuessFileIdx
// when body.guessFileIdx is truthy and nothing matched. Returns the
// guessedFileIdx value to append to the statistics, or missing.
struct CreateSelection final {
    Value guessedFileIdx = Value::missing();
    std::optional<std::string> error;
};
[[nodiscard]] CreateSelection createRouteSelection(const Value &body, const Value &files);

} // namespace server1::policy
