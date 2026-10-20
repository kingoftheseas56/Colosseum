#include "AnnotationMeaningPlane.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QSet>
#include <algorithm>

namespace vinyl {
namespace {

template <typename T>
AnnotationResult<T> fail(AnnotationErrorCode code,
                         QString field,
                         QString detail)
{
    return AnnotationResult<T>::failure(
        code, std::move(field), std::move(detail));
}

bool hasControlCharacter(const QString &value)
{
    for (const QChar ch : value) {
        const ushort code = ch.unicode();
        if (code < 0x20 || code == 0x7f)
            return true;
    }
    return false;
}

bool containsReferent(const QVector<AnnotationReferent> &referents,
                      const AnnotationReferentId &id)
{
    for (const AnnotationReferent &referent : referents) {
        if (referent.id() == id)
            return true;
    }
    return false;
}

} // namespace

QString annotationErrorCodeName(AnnotationErrorCode code)
{
    switch (code) {
    case AnnotationErrorCode::None:
        return QStringLiteral("none");
    case AnnotationErrorCode::InvalidAnnotationId:
        return QStringLiteral("invalid_annotation_id");
    case AnnotationErrorCode::InvalidReferent:
        return QStringLiteral("invalid_referent");
    case AnnotationErrorCode::InvalidTextAnchor:
        return QStringLiteral("invalid_text_anchor");
    case AnnotationErrorCode::InvalidSummary:
        return QStringLiteral("invalid_summary");
    case AnnotationErrorCode::InvalidProvenance:
        return QStringLiteral("invalid_provenance");
    case AnnotationErrorCode::InvalidBody:
        return QStringLiteral("invalid_body");
    case AnnotationErrorCode::DuplicateReferentId:
        return QStringLiteral("duplicate_referent_id");
    case AnnotationErrorCode::DuplicateAnnotationId:
        return QStringLiteral("duplicate_annotation_id");
    case AnnotationErrorCode::UnknownReferent:
        return QStringLiteral("unknown_referent");
    case AnnotationErrorCode::SourceDocumentMismatch:
        return QStringLiteral("source_document_mismatch");
    case AnnotationErrorCode::UnknownTimedTextCue:
        return QStringLiteral("unknown_timed_text_cue");
    case AnnotationErrorCode::RecordingMismatch:
        return QStringLiteral("recording_mismatch");
    }
    return QStringLiteral("unknown");
}

QString annotationBodyStateName(AnnotationBodyState state)
{
    switch (state) {
    case AnnotationBodyState::NotLoaded:
        return QStringLiteral("not_loaded");
    case AnnotationBodyState::Loaded:
        return QStringLiteral("loaded");
    case AnnotationBodyState::Unavailable:
        return QStringLiteral("unavailable");
    case AnnotationBodyState::Removed:
        return QStringLiteral("removed");
    }
    return QStringLiteral("unknown");
}

QString normalizeAnnotationPlainText(const QString &text)
{
    const QString normalized =
        text.normalized(QString::NormalizationForm_KC);
    QString out;
    out.reserve(normalized.size());

    bool pendingSpace = false;
    for (const QChar ch : normalized) {
        if (ch.isSpace()) {
            if (!out.isEmpty())
                pendingSpace = true;
            continue;
        }

        if (pendingSpace) {
            out.append(QLatin1Char(' '));
            pendingSpace = false;
        }
        out.append(ch);
    }

    return out;
}

AnnotationResult<AnnotationId> AnnotationId::create(const QString &value)
{
    if (value.isEmpty()
        || value.size() > 256
        || value.trimmed() != value) {
        return fail<AnnotationId>(
            AnnotationErrorCode::InvalidAnnotationId,
            QStringLiteral("annotationId"),
            QStringLiteral(
                "Annotation ID must be non-empty, bounded, and have no surrounding whitespace."));
    }
    if (hasControlCharacter(value)) {
        return fail<AnnotationId>(
            AnnotationErrorCode::InvalidAnnotationId,
            QStringLiteral("annotationId"),
            QStringLiteral(
                "Annotation ID must not contain control characters."));
    }

    return AnnotationResult<AnnotationId>::success(
        AnnotationId(value));
}

AnnotationResult<AnnotationTextAnchor> AnnotationTextAnchor::create(
    const QString &sourceText,
    const QString &fragment,
    qsizetype occurrence)
{
    if (occurrence < 0) {
        return fail<AnnotationTextAnchor>(
            AnnotationErrorCode::InvalidTextAnchor,
            QStringLiteral("occurrence"),
            QStringLiteral("Occurrence must be non-negative."));
    }

    const QString normalizedText =
        normalizeAnnotationPlainText(sourceText);
    const QString normalizedFragment =
        normalizeAnnotationPlainText(fragment);

    if (normalizedText.isEmpty() || normalizedFragment.isEmpty()) {
        return fail<AnnotationTextAnchor>(
            AnnotationErrorCode::InvalidTextAnchor,
            QStringLiteral("textAnchor"),
            QStringLiteral(
                "Source text and fragment must remain non-empty after normalization."));
    }

    qsizetype start = -1;
    qsizetype searchFrom = 0;
    for (qsizetype index = 0; index <= occurrence; ++index) {
        start = normalizedText.indexOf(
            normalizedFragment,
            searchFrom,
            Qt::CaseSensitive);
        if (start < 0) {
            return fail<AnnotationTextAnchor>(
                AnnotationErrorCode::InvalidTextAnchor,
                QStringLiteral("fragment"),
                QStringLiteral(
                    "Normalized fragment occurrence was not found in normalized source text."));
        }
        searchFrom = start + normalizedFragment.size();
    }

    return AnnotationResult<AnnotationTextAnchor>::success(
        AnnotationTextAnchor(
            normalizedText,
            start,
            normalizedFragment.size()));
}

QString AnnotationTextAnchor::fragment() const
{
    return m_normalizedText.mid(m_rangeStart, m_rangeLength);
}

AnnotationReferentId AnnotationReferent::deriveId(
    const RecordingMbid &recordingMbid,
    const std::optional<TimedTextDocumentId> &sourceDocumentId,
    const std::optional<TimedTextCueId> &cueId,
    const std::optional<AnnotationTextAnchor> &textAnchor)
{
    QByteArray material;
    const auto field = [&material](const QByteArray &value) {
        material += QByteArray::number(value.size()) + ':' + value;
    };
    field(QByteArrayLiteral("vinyl-annotation-referent-v2"));
    field(recordingMbid.toString().toUtf8());
    field(sourceDocumentId.has_value() ? QByteArrayLiteral("document") : QByteArrayLiteral("no-document"));
    if (sourceDocumentId) field(sourceDocumentId->value().toUtf8());
    field(cueId.has_value() ? QByteArrayLiteral("cue") : QByteArrayLiteral("no-cue"));
    if (cueId) field(cueId->value().toUtf8());
    field(textAnchor.has_value() ? QByteArrayLiteral("anchor") : QByteArrayLiteral("no-anchor"));
    if (textAnchor) {
        field(textAnchor->normalizedText().toUtf8());
        field(QByteArray::number(textAnchor->rangeStart()));
        field(QByteArray::number(textAnchor->rangeLength()));
    }

    const QByteArray digest = QCryptographicHash::hash(
        material,
        QCryptographicHash::Sha256).toHex();

    return AnnotationReferentId(
        QStringLiteral("ref:v2:sha256:")
        + QString::fromLatin1(digest));
}

AnnotationResult<AnnotationReferent> AnnotationReferent::create(
    RecordingMbid recordingMbid,
    std::optional<TimedTextDocumentId> sourceDocumentId,
    std::optional<TimedTextCueId> cueId,
    std::optional<AnnotationTextAnchor> textAnchor)
{
    if (!cueId.has_value() && !textAnchor.has_value()) {
        return fail<AnnotationReferent>(
            AnnotationErrorCode::InvalidReferent,
            QStringLiteral("referent"),
            QStringLiteral(
                "A referent requires a BC1 cue identity, a normalized text anchor, or both."));
    }
    if (cueId.has_value() != sourceDocumentId.has_value()) {
        return fail<AnnotationReferent>(
            AnnotationErrorCode::InvalidReferent,
            QStringLiteral("sourceDocumentId"),
            QStringLiteral(
                "Cue-linked referents require the exact BC1 timed-text document identity; untimed referents must not invent one."));
    }

    const AnnotationReferentId id =
        deriveId(recordingMbid, sourceDocumentId, cueId, textAnchor);

    return AnnotationResult<AnnotationReferent>::success(
        AnnotationReferent(
            id,
            std::move(recordingMbid),
            std::move(sourceDocumentId),
            std::move(cueId),
            std::move(textAnchor)));
}

AnnotationResult<AnnotationReferent> AnnotationReferent::cueLinked(
    const TimedTextDocument &sourceDocument,
    TimedTextCueId cueId,
    std::optional<AnnotationTextAnchor> textAnchor)
{
    const auto cue = std::find_if(
        sourceDocument.cues().cbegin(),
        sourceDocument.cues().cend(),
        [&cueId](const TimedTextCue &candidate) {
            return candidate.id() == cueId;
        });
    if (cue == sourceDocument.cues().cend()) {
        return fail<AnnotationReferent>(
            AnnotationErrorCode::UnknownTimedTextCue,
            QStringLiteral("cueId"),
            QStringLiteral(
                "Cue-linked annotation must reference a cue owned by the exact BC1 source document."));
    }

    if (textAnchor && textAnchor->normalizedText() != normalizeAnnotationPlainText(cue->text())) {
        return fail<AnnotationReferent>(AnnotationErrorCode::InvalidTextAnchor,
            QStringLiteral("textAnchor"), QStringLiteral("Fragment anchor must use the selected cue's normalized text."));
    }
    return create(
        sourceDocument.recordingMbid(),
        std::optional<TimedTextDocumentId>{sourceDocument.documentId()},
        std::optional<TimedTextCueId>{std::move(cueId)},
        std::move(textAnchor));
}

AnnotationResult<AnnotationReferent> AnnotationReferent::untimed(
    RecordingMbid recordingMbid,
    AnnotationTextAnchor textAnchor)
{
    return create(
        std::move(recordingMbid),
        std::nullopt,
        std::nullopt,
        std::optional<AnnotationTextAnchor>{
            std::move(textAnchor)});
}

AnnotationResult<qsizetype> AnnotationReferent::resolveCueIndexIn(
    const TimedTextDocument &sourceDocument) const
{
    if (!m_cueId.has_value() || !m_sourceDocumentId.has_value()) {
        return fail<qsizetype>(
            AnnotationErrorCode::InvalidReferent,
            QStringLiteral("referent"),
            QStringLiteral(
                "Untimed annotation referent has no BC1 cue to resolve."));
    }
    if (sourceDocument.recordingMbid() != m_recordingMbid) {
        return fail<qsizetype>(
            AnnotationErrorCode::RecordingMismatch,
            QStringLiteral("recordingMbid"),
            QStringLiteral(
                "Annotation referent belongs to another Recording MBID."));
    }
    if (sourceDocument.documentId() != *m_sourceDocumentId) {
        return fail<qsizetype>(
            AnnotationErrorCode::SourceDocumentMismatch,
            QStringLiteral("sourceDocumentId"),
            QStringLiteral(
                "Annotation referent belongs to another timed-text document revision."));
    }

    for (qsizetype index = 0; index < sourceDocument.cues().size(); ++index) {
        if (sourceDocument.cues().at(index).id() == *m_cueId) {
            if (m_textAnchor && m_textAnchor->normalizedText()
                    != normalizeAnnotationPlainText(sourceDocument.cues().at(index).text())) {
                return fail<qsizetype>(AnnotationErrorCode::InvalidTextAnchor,
                    QStringLiteral("textAnchor"), QStringLiteral("Fragment anchor does not belong to this cue."));
            }
            return AnnotationResult<qsizetype>::success(index);
        }
    }
    return fail<qsizetype>(
        AnnotationErrorCode::UnknownTimedTextCue,
        QStringLiteral("cueId"),
        QStringLiteral(
            "Annotation cue identity is absent from its claimed BC1 document revision."));
}

AnnotationResult<AnnotationProvider> AnnotationProvider::create(
    ProviderId providerId,
    std::optional<ProviderItemId> sourceItemId,
    std::optional<QUrl> sourceUrl)
{
    if (sourceUrl.has_value()) {
        const QString scheme = sourceUrl->scheme().toLower();
        if (!sourceUrl->isValid()
            || sourceUrl->isRelative()
            || (scheme != QStringLiteral("http")
                && scheme != QStringLiteral("https"))
            || !sourceUrl->userInfo().isEmpty()) {
            return fail<AnnotationProvider>(
                AnnotationErrorCode::InvalidProvenance,
                QStringLiteral("sourceUrl"),
                QStringLiteral(
                    "Source URL must be an absolute HTTP(S) URL without embedded credentials."));
        }
    }

    return AnnotationResult<AnnotationProvider>::success(
        AnnotationProvider(
            std::move(providerId),
            std::move(sourceItemId),
            std::move(sourceUrl)));
}

AnnotationResult<AnnotationSummary> AnnotationSummary::create(
    QString header,
    QString summary)
{
    header = normalizeAnnotationPlainText(header);
    summary = normalizeAnnotationPlainText(summary);

    if (header.isEmpty()
        || header.size() > 512
        || summary.size() > 4096) {
        return fail<AnnotationSummary>(
            AnnotationErrorCode::InvalidSummary,
            QStringLiteral("summary"),
            QStringLiteral(
                "Header must be non-empty and header/summary must remain within bounds."));
    }

    return AnnotationResult<AnnotationSummary>::success(
        AnnotationSummary(
            std::move(header),
            std::move(summary)));
}

AnnotationBody AnnotationBody::notLoaded()
{
    return AnnotationBody(AnnotationBodyState::NotLoaded);
}

AnnotationResult<AnnotationBody> AnnotationBody::loadedPlainText(
    QString plainText)
{
    plainText = normalizeAnnotationPlainText(plainText);

    if (plainText.isEmpty() || plainText.size() > 262144) {
        return fail<AnnotationBody>(
            AnnotationErrorCode::InvalidBody,
            QStringLiteral("body.plainText"),
            QStringLiteral(
                "Loaded annotation body must be non-empty normalized plain text within bounds."));
    }

    return AnnotationResult<AnnotationBody>::success(
        AnnotationBody(
            AnnotationBodyState::Loaded,
            std::move(plainText)));
}

AnnotationBody AnnotationBody::unavailable()
{
    return AnnotationBody(AnnotationBodyState::Unavailable);
}

AnnotationBody AnnotationBody::removed()
{
    return AnnotationBody(AnnotationBodyState::Removed);
}

AnnotationResult<AnnotationDocument> AnnotationDocument::create(
    RecordingMbid recordingMbid,
    QVector<AnnotationReferent> referents,
    QVector<Annotation> annotations)
{
    QSet<QString> referentIds;
    for (qsizetype i = 0; i < referents.size(); ++i) {
        const AnnotationReferent &referent = referents.at(i);

        if (referent.recordingMbid() != recordingMbid) {
            return fail<AnnotationDocument>(
                AnnotationErrorCode::RecordingMismatch,
                QStringLiteral(
                    "referents[%1].recordingMbid").arg(i),
                QStringLiteral(
                    "Every referent must belong to the document Recording MBID."));
        }

        if (referentIds.contains(referent.id().value())) {
            return fail<AnnotationDocument>(
                AnnotationErrorCode::DuplicateReferentId,
                QStringLiteral("referents[%1].id").arg(i),
                QStringLiteral(
                    "Referent identity must be unique within one document."));
        }
        referentIds.insert(referent.id().value());
    }

    QSet<QString> annotationIds;
    for (qsizetype i = 0; i < annotations.size(); ++i) {
        const Annotation &annotation = annotations.at(i);

        if (annotation.recordingMbid() != recordingMbid) {
            return fail<AnnotationDocument>(
                AnnotationErrorCode::RecordingMismatch,
                QStringLiteral(
                    "annotations[%1].recordingMbid").arg(i),
                QStringLiteral(
                    "Every annotation must preserve the document Recording MBID."));
        }

        if (annotationIds.contains(annotation.id().value())) {
            return fail<AnnotationDocument>(
                AnnotationErrorCode::DuplicateAnnotationId,
                QStringLiteral("annotations[%1].id").arg(i),
                QStringLiteral(
                    "Annotation identity must be unique within one document."));
        }
        annotationIds.insert(annotation.id().value());

        if (!containsReferent(
                referents,
                annotation.referentId())) {
            return fail<AnnotationDocument>(
                AnnotationErrorCode::UnknownReferent,
                QStringLiteral(
                    "annotations[%1].referentId").arg(i),
                QStringLiteral(
                    "Annotation referent must exist in the same document."));
        }
    }

    return AnnotationResult<AnnotationDocument>::success(
        AnnotationDocument(
            std::move(recordingMbid),
            std::move(referents),
            std::move(annotations)));
}

QVector<Annotation> AnnotationDocument::annotationsFor(
    const AnnotationReferentId &referentId) const
{
    QVector<Annotation> out;
    for (const Annotation &annotation : m_annotations) {
        if (annotation.referentId() == referentId)
            out.push_back(annotation);
    }
    return out;
}

} // namespace vinyl
