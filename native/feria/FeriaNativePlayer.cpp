#include "FeriaNativePlayer.h"
#include "../player/mpvitem.h"
#include <QUuid>
#include <cmath>
#include <algorithm>

FeriaNativePlayer::FeriaNativePlayer(QObject *parent) : QObject(parent) {
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(30000);
    connect(&m_deadline,&QTimer::timeout,this,[this] {
        if (!active() || m_ready) return;
        m_error=QStringLiteral("The video did not become ready. Return to the website player and try again.");
        m_tick.stop();
        if (m_player) { m_player->setPause(true); m_player->command({QStringLiteral("stop")}); }
        publish();
    });
    m_tick.setInterval(100);
    connect(&m_tick,&QTimer::timeout,this,[this] {
        if (m_player && m_loaded && !m_ready && m_player->decodedWidth()>0
                && !m_player->coreSeeking() && std::abs(m_player->position()-m_start)<1) {
            m_ready=true;
            m_deadline.stop();
            m_player->setPause(m_paused || m_suspended);
            m_resumeOwned=m_suspended && !m_paused;
        }
        publish();
    });
}
bool FeriaNativePlayer::allowsSource(const QUrl &source) {
    if (!source.isValid() || source.host().isEmpty() || !source.userInfo().isEmpty()) return false;
    if (source.scheme()!=QStringLiteral("http") && source.scheme()!=QStringLiteral("https")) return false;
    const QString text=source.toString(QUrl::FullyEncoded);
    return text.size()<=8192 && !text.contains('\r') && !text.contains('\n');
}
bool FeriaNativePlayer::openSource(const QUrl &source,const QString &document,double position,
        bool paused,int volume,double rate,const QVariantMap &headers,const QUrl &audioSource) {
    if (!allowsSource(source) || document.isEmpty() || !std::isfinite(position) || position<0
            || !std::isfinite(rate) || rate<=0 || rate>4
            || (!audioSource.isEmpty() && !allowsSource(audioSource))) return false;
    close();
    m_source=source; m_audioSource=audioSource; m_document=document;
    m_session=QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_start=position; m_paused=paused; m_volume=std::clamp(volume,0,100); m_rate=rate;
    m_headers=headers; m_loaded=false; m_ready=false; m_ended=false; m_error.clear();
    m_deadline.start(); emit activeChanged(); publish(); return true;
}
void FeriaNativePlayer::bindSurface(QObject *object) {
    auto *player=qobject_cast<MpvItem *>(object);
    if (!active() || !player || m_player) return;
    m_player=player;
    const QString session=m_session;
    connect(player,&MpvItem::ready,this,[this,player,session] {
        if (m_session!=session || m_player!=player) return;
        player->setPause(true);
        player->setVolume(m_volume); player->setSpeed(m_rate);
        if(!m_audioSource.isEmpty())player->setPropertyBlocking(QStringLiteral("audio-files"),QStringList{m_audioSource.toString(QUrl::FullyEncoded)});
        player->loadFileWithHeaders(m_source.toString(QUrl::FullyEncoded),m_headers);
        m_tick.start();
    },Qt::QueuedConnection);
    connect(player,&MpvItem::fileLoaded,this,[this,player,session] {
        if (m_session!=session || m_player!=player) return;
        m_loaded=true;
        if (player->duration()>0) m_start=std::min(m_start,std::max(0.,player->duration()-1));
        if (m_start>0) player->seekExact(m_start);
        publish();
    });
    connect(player,&MpvItem::endFile,this,[this,session](const QString &reason) {
        if (m_session!=session) return;
        if (reason=="eof") m_ended=true;
        else if (reason=="error" || reason=="other") {
            m_error=QStringLiteral("This source could not be played. Return to the website player.");
            m_deadline.stop(); m_tick.stop();
        }
        publish();
    });
}
void FeriaNativePlayer::close() {
    const bool wasActive=active();
    m_tick.stop(); m_deadline.stop(); m_session.clear(); m_document.clear(); m_loaded=false; m_ready=false;
    m_resumeOwned=false; m_headers.clear(); m_source={}; m_audioSource={};
    if (m_player) { disconnect(m_player,nullptr,this,nullptr); m_player->setPause(true); m_player->command({QStringLiteral("stop")}); }
    m_player=nullptr;
    if (wasActive) { emit activeChanged(); publish(); }
}
void FeriaNativePlayer::setSuspended(bool suspended) {
    if (suspended==m_suspended) return;
    m_suspended=suspended;
    if (!m_player || !m_ready) return;
    if (suspended) { m_resumeOwned=!m_player->pause() && !m_ended && m_error.isEmpty(); m_player->setPause(true); }
    else { const bool resume=m_resumeOwned; m_resumeOwned=false; if(resume && !m_ended && m_error.isEmpty())m_player->setPause(false); }
    publish();
}
QVariantMap FeriaNativePlayer::bootstrap() const { return snapshot(); }
QVariantMap FeriaNativePlayer::snapshot() const {
    return {{"document",m_document},{"session",m_session},{"ready",m_ready},{"ended",m_ended},{"error",m_error},
        {"position",m_player?m_player->position():0},{"duration",m_player?m_player->duration():0},
        {"paused",!m_player || m_player->pause()},{"volume",m_player?m_player->volume():m_volume},
        {"rate",m_player?m_player->speed():m_rate},{"suspended",m_suspended},
        {"audioTracks",m_player?m_player->audioTracks().size():0}};
}
void FeriaNativePlayer::publish() { emit stateChanged(); emit state(snapshot()); }
QVariantMap FeriaNativePlayer::act(const QString &document,const QString &session,const QString &action,double value) {
    if (!active() || document!=m_document || session!=m_session || !std::isfinite(value)) return {{"ok",false}};
    if (action=="close") { emit closeRequested(); return {{"ok",true}}; }
    if (action=="fullscreen") { emit fullscreenRequested(); return {{"ok",true}}; }
    if (!m_player || !m_ready || m_suspended) return {{"ok",false}};
    if (action=="play") { m_player->setPause(false); m_ended=false; }
    else if (action=="pause") m_player->setPause(true);
    else if (action=="seek" && value>=0 && value<=m_player->duration()) m_player->seekExact(value);
    else if (action=="volume" && value>=0 && value<=100) m_player->setVolume(qRound(value));
    else if (action=="rate" && value>0 && value<=4) m_player->setSpeed(value);
    else return {{"ok",false}};
    publish(); return {{"ok",true}};
}
