#include "VinylLyricsService.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

using namespace vinyl;
namespace {
bool number(const QVariant &value,double *out) {
    if(!value.isValid()||value.metaType().id()==QMetaType::Bool
        ||value.metaType().id()==QMetaType::QString)return false;
    bool ok=false;const auto n=value.toDouble(&ok);
    if(!ok||!std::isfinite(n))return false;
    *out=n;return true;
}
QString folded(QString text) {
    text=text.normalized(QString::NormalizationForm_KD).toCaseFolded();
    text.remove(QRegularExpression("[\\p{M}]"));
    return text.replace(QChar(0x2019),'\'').replace(QChar(0x2010),'-').simplified();
}
bool signature(const QVariantMap &context) {
    auto ref=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(context.value("recording").toMap()));
    double seconds=0;
    return ref.ok()&&ref.value->kind()==CanonicalEntityKind::Recording
        &&QueueOccurrenceId::parse(context.value("occurrenceId").toString()).ok()
        &&ProfileScopeId::parse(context.value("profileScope").toString()).ok()
        &&!context.value("trackName").toString().trimmed().isEmpty()
        &&!context.value("artistName").toString().trimmed().isEmpty()
        &&context.value("trackName").toString().size()<=300
        &&context.value("artistName").toString().size()<=300
        &&context.value("albumName").toString().size()<=300
        &&number(context.value("duration"),&seconds)&&seconds>=1&&seconds<=3600;
}
TimedTextResult<TimedTextDocument> embedded(const QVariantMap &context) {
    const auto parsed=RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(context.value("recording").toMap()));
    if(!parsed.ok()||parsed.value->kind()!=CanonicalEntityKind::Recording
        ||!QueueOccurrenceId::parse(context.value("occurrenceId").toString()).ok()
        ||!ProfileScopeId::parse(context.value("profileScope").toString()).ok())
        return TimedTextResult<TimedTextDocument>::failure(TimedTextErrorCode::InvalidDocumentShape,"context");
    const auto &ref=*parsed.value;
    const auto item=ref.isCanonical()?ProviderItemId::parse(ref.mbidString()).value
        :std::optional<ProviderItemId>(ref.providerRef()->itemId());
    const TimedTextProvenance source(*ProviderId::parse("local").value,item);
    const auto raw=context.value("embeddedLyrics");
    if(raw.metaType().id()==QMetaType::QString&&raw.toString().size()<=256*1024)
        return raw.toString().trimmed().isEmpty()?TimedTextDocument::unavailable(ref,source)
            :TimedTextDocument::plain(ref,source,raw.toString().split('\n',Qt::SkipEmptyParts).join('\n'));
    if(raw.metaType().id()!=QMetaType::QVariantList||raw.toList().size()>10000
        ||QJsonDocument::fromVariant(raw).toJson(QJsonDocument::Compact).size()>512*1024)
        return TimedTextResult<TimedTextDocument>::failure(TimedTextErrorCode::InvalidDocumentShape,"embeddedLyrics");
    QVector<TimedTextCue> cues;QStringList plain;bool allTimed=true;
    for(const auto &entry:raw.toList()) {
        const auto row=entry.toMap();if(row.value("text").metaType().id()!=QMetaType::QString)
            return TimedTextResult<TimedTextDocument>::failure(TimedTextErrorCode::InvalidDocumentShape,"embeddedLyrics");
        const auto text=row.value("text").toString();plain.append(text);double seconds=0;
        if(!number(row.value("time",row.value("at")),&seconds)||seconds<0||seconds>3600)allTimed=false;
        else cues.append(TimedTextCue(*TimedTextCueId::create(QString("embedded:%1").arg(plain.size()-1)).value,text,qRound64(seconds*1000)));
    }
    if(allTimed&&!cues.isEmpty()) {
        auto result=TimedTextDocument::synchronized(ref,source,TimedTextKind::LineSynced,cues);
        if(result.ok())return result;
    }
    return plain.join('\n').trimmed().isEmpty()?TimedTextDocument::unavailable(ref,source)
        :TimedTextDocument::plain(ref,source,plain.join('\n'));
}
TimedTextResult<TimedTextDocument> invalid(const QString &field,const QString &detail) {
    return TimedTextResult<TimedTextDocument>::failure(TimedTextErrorCode::InvalidDocumentShape,field,detail);
}
TimedTextResult<TimedTextDocument> parseLrc(const RecommendationEntityRef &ref,
    const TimedTextProvenance &source,const QString &text,double duration) {
    static const QRegularExpression timestamp("^\\[(\\d{1,3}):([0-5]\\d)(?:\\.(\\d{1,3}))?\\]");
    static const QRegularExpression metadata("^\\[(ar|al|ti|au|by|re|ve|length):[^\\r\\n]*\\]$",QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression offset("^\\[offset:([+-]?\\d{1,6})\\]$",QRegularExpression::CaseInsensitiveOption);
    QVector<TimedTextCue> cues;bool offsetSeen=false;qint64 offsetMs=0;
    auto fail=[] {return TimedTextResult<TimedTextDocument>::failure(
        TimedTextErrorCode::InvalidTiming,"syncedLyrics","The lyric timestamps are invalid.");};
    for(auto raw:text.split('\n')) {
        raw=raw.trimmed();if(raw.isEmpty())continue;
        auto match=offset.match(raw);
        if(match.hasMatch()) {
            if(offsetSeen)return fail();offsetSeen=true;offsetMs=match.captured(1).toLongLong();
            if(qAbs(offsetMs)>60000)return fail();continue;
        }
        if(metadata.match(raw).hasMatch())continue;
        match=timestamp.match(raw);if(!match.hasMatch())return fail();
        // BC1 requires monotonically ordered cues. Unsupported multi-stamp or
        // word-timed input may use genuine plain text, never fabricated timing.
        const auto fraction=match.captured(3).leftJustified(3,'0');
        const qint64 start=(match.captured(1).toLongLong()*60+match.captured(2).toLongLong())*1000+fraction.toInt();
        auto body=raw.mid(match.capturedLength());
        if(timestamp.match(body).hasMatch()||body.contains(QRegularExpression("<\\d+:\\d"))||start>qRound64(duration*1000)+2000)return fail();
        if(!cues.isEmpty()&&start<cues.last().startMs())return fail();
        if(!cues.isEmpty()&&start==cues.last().startMs()) {
            const auto previous=cues.takeLast();
            cues.append(TimedTextCue(previous.id(),previous.text().isEmpty()?body:
                body.isEmpty()?previous.text():previous.text()+"\n"+body,start));
        }else cues.append(TimedTextCue(*TimedTextCueId::create(QString("lrc:%1").arg(cues.size())).value,body,start));
        if(cues.size()>10000)return fail();
    }
    return TimedTextDocument::synchronized(ref,source,TimedTextKind::LineSynced,std::move(cues),offsetMs);
}
}

TimedTextResult<TimedTextDocument> VinylLyricsService::fromLrclib(
    const QVariantMap &context,const QVariantMap &record) {
    if(!signature(context))return invalid("context","The selected recording has no complete lyric signature.");
    double expected=0,actual=0,id=0;number(context.value("duration"),&expected);
    if(!number(record.value("id"),&id)||id<1||std::floor(id)!=id||id>9007199254740991.0
        ||!number(record.value("duration"),&actual)||actual<1||actual>3600||std::abs(expected-actual)>2)
        return invalid("record","The lyric record does not match the selected duration.");
    for(const auto &field:{"trackName","artistName","albumName"}) {
        if(record.value(field).metaType().id()!=QMetaType::QString||record.value(field).toString().size()>300
            ||(!context.value(field).toString().isEmpty()&&folded(context.value(field).toString())!=folded(record.value(field).toString())))
            return invalid(field,"The lyric record does not match the selected version.");
    }
    if(record.value("instrumental").metaType().id()!=QMetaType::Bool)return invalid("instrumental","Invalid lyric record.");
    for(const auto &field:{"plainLyrics","syncedLyrics"}) {
        const auto value=record.value(field);
        if((value.isValid()&&!value.isNull()&&value.metaType().id()!=QMetaType::QString)||value.toString().size()>256*1024)
            return invalid(field,"Invalid lyric text.");
    }
    const auto ref=*RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(context.value("recording").toMap())).value;
    const TimedTextProvenance source(*ProviderId::parse("lrclib").value,
        *ProviderItemId::parse(QString::number(qint64(id))).value);
    const auto plain=record.value("plainLyrics").toString().trimmed(),synced=record.value("syncedLyrics").toString().trimmed();
    if(record.value("instrumental").toBool()) {
        if(!plain.isEmpty()||!synced.isEmpty())return invalid("instrumental","An instrumental record contains lyric text.");
        return TimedTextDocument::unavailable(ref,source);
    }
    if(!synced.isEmpty()) {
        auto timed=parseLrc(ref,source,synced,actual);
        if(timed.ok())return timed;
        if(plain.isEmpty())return timed;
    }
    return plain.isEmpty()?TimedTextDocument::unavailable(ref,source):TimedTextDocument::plain(ref,source,plain);
}

VinylLyricsService::~VinylLyricsService() { cancel(); }
void VinylLyricsService::cancel() {
    m_request=-1;
    if(m_provider)QMetaObject::invokeMethod(m_provider,"cancel",Qt::DirectConnection,Q_ARG(QString,QString("lyrics")));
}
void VinylLyricsService::setProvider(QObject *provider) {
    if(m_provider==provider)return;cancel();
    if(m_provider)disconnect(m_provider,nullptr,this,nullptr);
    m_provider=provider;
    if(provider)connect(provider,SIGNAL(completed(QString,int,QVariant,QString)),this,SLOT(completed(QString,int,QVariant,QString)));
    emit providerChanged();request();
}
void VinylLyricsService::setContext(const QVariantMap &context) {
    if(m_context==context)return;cancel();m_context=context;m_document.reset();m_error.clear();m_status="unavailable";
    emit contextChanged();emit documentChanged();emit currentLineChanged();request();
}
void VinylLyricsService::request(bool refresh) {
    cancel();
    if(m_context.contains("embeddedLyrics")) {
        auto result=embedded(m_context);m_error.clear();
        if(result.ok()) {
            m_document=*result.value;
            m_status=m_document->kind()==TimedTextKind::Plain?"plain":m_document->kind()==TimedTextKind::LineSynced?"line_synced":"unavailable";
        }else {m_document.reset();m_status="error";m_error="The supplied lyrics are invalid.";}
        emit documentChanged();emit currentLineChanged();return;
    }
    if(!m_provider||!signature(m_context))return;
    const QString lane="lyrics",operation="lyrics";
    const auto ref=*RecommendationEntityRef::fromJson(QJsonObject::fromVariantMap(m_context.value("recording").toMap())).value;
    const QString value=ref.stableKey();QVariantMap payload;
    for(const auto &field:{"trackName","artistName","albumName","duration"})payload[field]=m_context.value(field);
    if(refresh)payload["refresh"]=true;
    m_status="loading";m_error.clear();emit documentChanged();
    int result=-1;
    const bool invoked=QMetaObject::invokeMethod(m_provider,"requestPayload",Qt::DirectConnection,Q_RETURN_ARG(int,result),
        Q_ARG(QString,lane),Q_ARG(QString,operation),Q_ARG(QString,value),Q_ARG(QVariantMap,payload));
    m_request=invoked?result:-1;
    if(m_request<0){m_status="error";m_error="Lyrics could not be requested.";emit documentChanged();}
}
void VinylLyricsService::retry() { request(true); }
void VinylLyricsService::completed(const QString &lane,int requestId,const QVariant &data,const QString &error) {
    if(sender()!=m_provider||lane!="lyrics"||requestId<0||requestId!=m_request)return;
    m_request=-1;const auto envelope=data.toMap();m_error=error;
    if(m_error.isEmpty()&&(envelope.value("source")!="lrclib"
        ||!QDateTime::fromString(envelope.value("fetchedAt").toString(),Qt::ISODateWithMs).isValid()
        ||!QStringList{"found","not_found"}.contains(envelope.value("status").toString())))m_error="The lyric response is invalid.";
    if(m_error.isEmpty()&&envelope.value("status")=="not_found") {
        m_document.reset();m_status="unavailable";
    }else if(m_error.isEmpty()) {
        const auto result=fromLrclib(m_context,envelope.value("record").toMap());
        if(result.ok()) {
            m_document=*result.value;
            m_status=m_document->kind()==TimedTextKind::Unavailable
                ?(envelope.value("record").toMap().value("instrumental").toBool()?"instrumental":"unavailable")
                :m_document->kind()==TimedTextKind::Plain?"plain":"line_synced";
        }else m_error="Lyrics did not match this song or contained invalid timing.";
    }
    if(!m_error.isEmpty()){m_document.reset();m_status="error";}
    emit documentChanged();emit currentLineChanged();
}
QVariant VinylLyricsService::recordingTimeOffsetMs() const {return m_recordingOffset?QVariant::fromValue(*m_recordingOffset):QVariant();}
void VinylLyricsService::setRecordingTimeOffsetMs(const QVariant &offset) {
    double value=0;std::optional<qint64> parsed;
    if(number(offset,&value)&&std::floor(value)==value&&std::abs(value)<=3600000)parsed=qint64(value);
    if(parsed==m_recordingOffset)return;m_recordingOffset=parsed;emit documentChanged();emit currentLineChanged();
}
void VinylLyricsService::setPosition(double seconds) {
    if(!std::isfinite(seconds)||seconds<0)seconds=0;
    if(seconds==m_position)return;const auto before=currentLineIndex();m_position=seconds;emit positionChanged();
    if(before!=currentLineIndex())emit currentLineChanged();
}
bool VinylLyricsService::timed() const {return m_document&&m_document->kind()==TimedTextKind::LineSynced&&m_recordingOffset.has_value();}
QString VinylLyricsService::documentId() const {return m_document?m_document->documentId().value():QString();}
int VinylLyricsService::currentLineIndex() const {
    if(!timed()||m_position>3600)return -1;
    const auto index=currentTimedTextCueIndexForPlayback(*m_document,qRound64(m_position*1000),m_recordingOffset);
    return index?int(*index):-1;
}
QVariant VinylLyricsService::seekTime(int index) const {
    if(!timed())return {};
    const auto target=timedTextCuePlaybackSeekTargetMs(*m_document,index,m_recordingOffset);
    return target?QVariant(double(*target)/1000):QVariant();
}
QVariantList VinylLyricsService::lines() const {
    QVariantList result;if(!m_document)return result;
    if(m_document->kind()==TimedTextKind::Plain) {
        int index=0;for(const auto &line:m_document->plainText().split('\n'))
            result.append(QVariantMap{{"text",line},{"cueId",QString("plain:%1").arg(index++)}});
    }else for(int i=0;i<m_document->cues().size();++i) {
        const auto &cue=m_document->cues()[i];QVariantMap line{{"text",cue.text()},{"cueId",cue.id().value()}};
        const auto time=seekTime(i);if(time.isValid())line["time"]=time;result.append(line);
    }
    return result;
}
