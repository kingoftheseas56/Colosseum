// native/alignment/EnglishForcedAligner.cpp — see EnglishForcedAligner.h.
#include "alignment/EnglishForcedAligner.h"

#include "work/BackgroundWorkCoordinator.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace alignment {

EnglishForcedAligner::EnglishForcedAligner(QString modelPath, QString vocabPath)
    : m_modelPath(std::move(modelPath)), m_vocabPath(std::move(vocabPath))
{
    const QString dir = QFileInfo(m_modelPath).absolutePath();
    if (m_vocabPath.isEmpty())
        m_vocabPath = QDir(dir).filePath(QStringLiteral("vocab.json"));

    // 1) Integrity FIRST: validate the sibling manifest + checksum before any bytes
    //    are trusted. No usable manifest => fail closed as ModelMissing (mirrors the
    //    guided/coarse fail-closed posture).
    const QString manifestPath = QDir(dir).filePath(QStringLiteral("manifest.json"));
    models::ManifestError merr = models::ManifestError::None;
    m_manifest = models::ModelManifest::load(manifestPath, &merr);
    if (!m_manifest) {
        m_status = FailureCode::ModelMissing;
        return;
    }
    switch (m_manifest->validateChecksum()) {
    case models::ManifestError::FileMissing:
        m_status = FailureCode::ModelMissing;
        return;
    case models::ManifestError::ChecksumFailed:
        m_status = FailureCode::ModelChecksumFailed;
        return;
    default:
        break;
    }

    // 2) Vocabulary (single-char labels + blank/delimiter ids).
    if (!loadVocab()) {
        m_status = FailureCode::ModelMissing;
        return;
    }

    // 3) Create the CPU session ONCE. Single intra-op thread keeps inference
    //    deterministic (bit-parity with the numpy oracle) and off the media lane.
    //    The co-located wav2vec2_base_960h.onnx.data external-weights file is loaded
    //    automatically by ONNX Runtime from the model's directory.
    try {
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "english-forced-aligner");
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        const std::wstring wpath = m_manifest->filePath().toStdWString();
        m_session = std::make_unique<Ort::Session>(*m_env, wpath.c_str(), opts);

        Ort::AllocatorWithDefaultOptions alloc;
        m_inputName = m_session->GetInputNameAllocated(0, alloc).get();
        m_outputName = m_session->GetOutputNameAllocated(0, alloc).get();
    } catch (const Ort::Exception &) {
        m_session.reset();
        m_env.reset();
        m_status = FailureCode::AlignmentFailed;
        return;
    }

    m_status = FailureCode::None;
}

EnglishForcedAligner::~EnglishForcedAligner() = default;

bool EnglishForcedAligner::loadVocab()
{
    QFile f(m_vocabPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray labels = obj.value(QStringLiteral("labels")).toArray();
    if (labels.isEmpty())
        return false;

    m_labels.clear();
    m_charToId.assign(256, -1);
    for (int i = 0; i < labels.size(); ++i) {
        const QString lab = labels.at(i).toString();
        m_labels.push_back(lab.toStdString());
        if (lab.size() == 1) {
            const ushort u = lab.at(0).unicode();
            if (u < 256)
                m_charToId[u] = i;
        }
    }

    m_blankId = obj.value(QStringLiteral("pad_id")).toInt(0);
    const QString delim = obj.value(QStringLiteral("word_delimiter")).toString(QStringLiteral("|"));
    m_delimId = 4;
    if (delim.size() == 1) {
        const ushort u = delim.at(0).unicode();
        if (u < 256 && m_charToId[u] >= 0)
            m_delimId = m_charToId[u];
    }
    return true;
}

int EnglishForcedAligner::idForChar(QChar c) const
{
    const ushort u = c.toUpper().unicode();
    if (u < 256)
        return m_charToId[u];
    return -1;
}

ForcedAlignmentResult EnglishForcedAligner::align(const PcmWindow &window,
                                                  const CanonicalPassage &passage,
                                                  work::WorkContext &ctx) const
{
    ForcedAlignmentResult result;

    if (m_status != FailureCode::None || !m_session) {
        result.failure = (m_status == FailureCode::None) ? FailureCode::AlignmentFailed : m_status;
        return result;
    }

    // Yield / cancel BEFORE the heavy inference; a cancel is not a failure.
    if (!ctx.checkpoint()) {
        result.failure = FailureCode::None;
        return result;
    }
    if (window.samples.isEmpty()) {
        result.failure = FailureCode::AlignmentFailed;
        return result;
    }

    // ── 1) do_normalize: zero-mean / unit-variance over the window (population var,
    //       eps 1e-7), exactly as the Wav2Vec2 feature extractor. ─────────────────
    const int n = window.samples.size();
    std::vector<float> input(static_cast<size_t>(n));
    double mean = 0.0;
    for (int i = 0; i < n; ++i)
        mean += window.samples[i];
    mean /= n;
    double var = 0.0;
    for (int i = 0; i < n; ++i) {
        const double d = static_cast<double>(window.samples[i]) - mean;
        var += d * d;
    }
    var /= n;
    const double invStd = 1.0 / std::sqrt(var + 1e-7);
    for (int i = 0; i < n; ++i)
        input[i] = static_cast<float>((static_cast<double>(window.samples[i]) - mean) * invStd);

    // ── 2) ONNX inference: input_values [1,N] -> logits [1,frames,vocab] ──────────
    std::vector<float> logits;
    int frames = 0, vocab = 0;
    try {
        std::lock_guard<std::mutex> lk(m_runMutex);
        Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::array<int64_t, 2> ishape{1, static_cast<int64_t>(n)};
        Ort::Value in = Ort::Value::CreateTensor<float>(
            memInfo, input.data(), input.size(), ishape.data(), ishape.size());
        const char *inNames[] = {m_inputName.c_str()};
        const char *outNames[] = {m_outputName.c_str()};
        auto outputs =
            m_session->Run(Ort::RunOptions{nullptr}, inNames, &in, 1, outNames, 1);
        const auto shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 3) {
            result.failure = FailureCode::AlignmentFailed;
            return result;
        }
        frames = static_cast<int>(shape[1]);
        vocab = static_cast<int>(shape[2]);
        const float *out = outputs[0].GetTensorData<float>();
        logits.assign(out, out + static_cast<size_t>(frames) * vocab);
    } catch (const Ort::Exception &) {
        result.failure = FailureCode::AlignmentFailed;
        return result;
    }
    if (frames <= 0 || vocab <= 0) {
        result.failure = FailureCode::AlignmentFailed;
        return result;
    }

    // ── 3) log-softmax each frame (double, numerically stable) ───────────────────
    std::vector<double> logp(static_cast<size_t>(frames) * vocab);
    for (int t = 0; t < frames; ++t) {
        const float *row = logits.data() + static_cast<size_t>(t) * vocab;
        double m = row[0];
        for (int v = 1; v < vocab; ++v)
            if (row[v] > m) m = row[v];
        double sum = 0.0;
        for (int v = 0; v < vocab; ++v)
            sum += std::exp(static_cast<double>(row[v]) - m);
        const double lse = m + std::log(sum);
        double *dst = logp.data() + static_cast<size_t>(t) * vocab;
        for (int v = 0; v < vocab; ++v)
            dst[v] = static_cast<double>(row[v]) - lse;
    }
    auto LP = [&](int t, int lab) -> double {
        return logp[static_cast<size_t>(t) * vocab + lab];
    };

    // Frame -> absolute ms. Stride is the window's ACTUAL duration / frame count.
    const double windowMs = static_cast<double>(n) * 1000.0
                            / static_cast<double>(window.sampleRate > 0 ? window.sampleRate : 16000);
    const double stride = windowMs / frames;
    const double startMs0 = static_cast<double>(window.startMs);

    // ── 4) Tokenize the passage: words on [A-Za-z'] runs, uppercased letters -> ids
    //       (chars absent from the vocab are dropped), a '|' delimiter between words.
    struct WordTok { int startChar; int endChar; std::vector<int> tokenIdx; };
    std::vector<WordTok> words;
    std::vector<int> target; // flat token id sequence
    const QString &txt = passage.text;
    const int L = txt.size();
    auto isWordChar = [](QChar c) { return c.isLetter() || c == QLatin1Char('\''); };
    for (int i = 0; i < L;) {
        if (!isWordChar(txt.at(i))) { ++i; continue; }
        int j = i;
        std::vector<int> ids;
        while (j < L && isWordChar(txt.at(j))) {
            const int id = idForChar(txt.at(j));
            if (id >= 0)
                ids.push_back(id);
            ++j;
        }
        if (!ids.empty()) {
            WordTok w;
            w.startChar = i;
            w.endChar = j;
            if (!words.empty())
                target.push_back(m_delimId); // delimiter BETWEEN words, never at the ends
            for (int id : ids) {
                w.tokenIdx.push_back(static_cast<int>(target.size()));
                target.push_back(id);
            }
            words.push_back(std::move(w));
        }
        i = j;
    }

    const int K = static_cast<int>(target.size());
    if (K == 0) {
        result.ok = true; // nothing alignable, but not a failure
        return result;
    }

    // ── 5) CTC forced alignment (Viterbi) over the blank-interleaved state graph:
    //       [blank, t0, blank, t1, ..., t_{K-1}, blank]. Transitions: stay, advance,
    //       and skip-a-blank between two DIFFERENT labels. ──────────────────────────
    const int S = 2 * K + 1;
    std::vector<int> label(S, m_blankId);
    for (int k = 0; k < K; ++k)
        label[2 * k + 1] = target[k];

    constexpr double NEG = -1e30;
    std::vector<double> dp(static_cast<size_t>(frames) * S, NEG);
    std::vector<int> bp(static_cast<size_t>(frames) * S, -1);
    dp[0 * S + 0] = LP(0, label[0]);
    if (S > 1)
        dp[0 * S + 1] = LP(0, label[1]);
    for (int t = 1; t < frames; ++t) {
        const double *prev = dp.data() + static_cast<size_t>(t - 1) * S;
        double *cur = dp.data() + static_cast<size_t>(t) * S;
        int *bprow = bp.data() + static_cast<size_t>(t) * S;
        for (int s = 0; s < S; ++s) {
            int bestPrev = s;
            double bestVal = prev[s];                                    // stay
            if (s - 1 >= 0 && prev[s - 1] > bestVal) {                   // advance
                bestPrev = s - 1;
                bestVal = prev[s - 1];
            }
            if (s - 2 >= 0 && (s % 2 == 1) && label[s] != label[s - 2]   // skip a blank
                && prev[s - 2] > bestVal) {
                bestPrev = s - 2;
                bestVal = prev[s - 2];
            }
            cur[s] = bestVal + LP(t, label[s]);
            bprow[s] = bestPrev;
        }
    }

    // Backtrack from the better terminal state (last blank or last token).
    int endState = S - 1;
    if (S >= 2
        && dp[static_cast<size_t>(frames - 1) * S + (S - 2)]
               > dp[static_cast<size_t>(frames - 1) * S + (S - 1)])
        endState = S - 2;
    std::vector<int> path(frames, 0);
    {
        int s = endState;
        for (int t = frames - 1; t >= 0; --t) {
            path[t] = s;
            int p = bp[static_cast<size_t>(t) * S + s];
            if (p < 0)
                p = s;
            s = p;
        }
    }

    // ── 6) Per-token frame spans + per-frame aligned-path probability ────────────
    std::vector<int> spanFirst(K, -1), spanLast(K, -1);
    std::vector<double> fprob(frames);
    for (int t = 0; t < frames; ++t) {
        fprob[t] = std::exp(LP(t, label[path[t]]));
        const int st = path[t];
        if (st % 2 == 1) {
            const int k = (st - 1) / 2;
            if (spanFirst[k] < 0)
                spanFirst[k] = t;
            spanLast[k] = t;
        }
    }

    // Which word owns each target token (delimiters own nothing) -> per-word conf.
    std::vector<int> tokOwner(K, -1);
    for (int wi = 0; wi < static_cast<int>(words.size()); ++wi)
        for (int idx : words[wi].tokenIdx)
            tokOwner[idx] = wi;
    std::vector<double> wSum(words.size(), 0.0);
    std::vector<int> wCnt(words.size(), 0);
    for (int t = 0; t < frames; ++t) {
        const int st = path[t];
        if (st % 2 == 1) {
            const int owner = tokOwner[(st - 1) / 2];
            if (owner >= 0) {
                wSum[owner] += fprob[t];
                wCnt[owner] += 1;
            }
        }
    }

    // ── 7) Sentence spans: split on . ! ? (whole passage if none) ────────────────
    struct Sent { int cs; int ce; };
    std::vector<Sent> sents;
    int sstart = 0;
    for (int p = 0; p < L; ++p) {
        const QChar c = txt.at(p);
        if (c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?')) {
            const int end = p + 1;
            if (!txt.mid(sstart, end - sstart).trimmed().isEmpty())
                sents.push_back({sstart, end});
            sstart = end;
        }
    }
    if (sstart < L && !txt.mid(sstart).trimmed().isEmpty())
        sents.push_back({sstart, L});
    if (sents.empty())
        sents.push_back({0, L});

    auto toMs = [&](double frame) -> qint64 {
        return static_cast<qint64>(std::llround(startMs0 + frame * stride));
    };
    const int BIG = std::numeric_limits<int>::max();

    // Overall confidence = mean aligned-path prob over ALL letter frames.
    double allSum = 0.0;
    int allCnt = 0;
    for (size_t wi = 0; wi < words.size(); ++wi) {
        allSum += wSum[wi];
        allCnt += wCnt[wi];
    }
    result.confidence = allCnt > 0 ? allSum / allCnt : 0.0;

    // ── 8) Emit sentence + word cues (monotonic by construction) ─────────────────
    int sentOrdinal = 0;
    for (const Sent &se : sents) {
        std::vector<int> members;
        for (int wi = 0; wi < static_cast<int>(words.size()); ++wi)
            if (words[wi].startChar >= se.cs && words[wi].startChar < se.ce)
                members.push_back(wi);
        if (members.empty())
            continue;

        int sf0 = BIG, sf1 = -1;
        double sSum = 0.0;
        int sCnt = 0;
        int wordOrdinal = 0;
        for (int wi : members) {
            const WordTok &w = words[wi];
            int f0 = BIG, f1 = -1;
            for (int idx : w.tokenIdx) {
                if (spanFirst[idx] >= 0) {
                    f0 = std::min(f0, spanFirst[idx]);
                    sf0 = std::min(sf0, spanFirst[idx]);
                }
                if (spanLast[idx] >= 0) {
                    f1 = std::max(f1, spanLast[idx]);
                    sf1 = std::max(sf1, spanLast[idx]);
                }
            }
            if (f1 < 0) { f0 = 0; f1 = 0; } // degenerate guard (never hit for real tokens)
            WordCue wc;
            wc.sentenceOrdinal = sentOrdinal;
            wc.ordinal = wordOrdinal++;
            wc.startMs = toMs(f0);
            wc.endMs = toMs(f1 + 1);
            wc.canonicalStart = passage.canonicalStart + w.startChar;
            wc.canonicalEnd = passage.canonicalStart + w.endChar;
            wc.confidence = wCnt[wi] > 0 ? wSum[wi] / wCnt[wi] : 0.0;
            result.words.push_back(wc);
            sSum += wSum[wi];
            sCnt += wCnt[wi];
        }
        if (sf1 < 0) { sf0 = 0; sf1 = 0; }

        SentenceCue sc;
        sc.ordinal = sentOrdinal;
        sc.startMs = toMs(sf0);
        sc.endMs = toMs(sf1 + 1);
        sc.spineHref = passage.spineHref;
        sc.canonicalStart = passage.canonicalStart + se.cs;
        sc.canonicalEnd = passage.canonicalStart + se.ce;
        sc.sentenceHash = QString::fromLatin1(
            QCryptographicHash::hash(txt.mid(se.cs, se.ce - se.cs).toUtf8(),
                                     QCryptographicHash::Sha256)
                .toHex());
        sc.confidence = sCnt > 0 ? sSum / sCnt : 0.0;
        sc.regionKind = RegionKind::Aligned;
        result.sentences.push_back(sc);
        ++sentOrdinal;
    }

    result.ok = true;
    return result;
}

} // namespace alignment
