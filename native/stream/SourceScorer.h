#pragma once
#include "StreamTypes.h"
namespace colosseum::stream {
class SourceScorer {
  public:
    static CorpusStats computeCorpusStats(const QList<ParsedStream> &streams,
                                          const ScoreOptions &options);
    static ScoredStream score(const ParsedStream &stream, const ScoreOptions &options,
                              const CorpusStats &corpus);
    static RankedPicker rankAndPick(QList<ScoredStream> streams, const ScoreOptions &options);
    static RankedPicker evaluate(const QList<Stream> &streams, const TrustOptions &trust,
                                 const ScoreOptions &score);
};
} // namespace colosseum::stream
