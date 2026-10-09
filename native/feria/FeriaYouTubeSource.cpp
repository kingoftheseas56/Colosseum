#include "FeriaYouTubeSource.h"
#include "FeriaNativePlayer.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>

FeriaYouTubeSource::FeriaYouTubeSource(QObject *parent):QObject(parent) {
    m_deadline.setSingleShot(true); m_deadline.setInterval(60000);
    connect(&m_deadline,&QTimer::timeout,this,[this] {
        const auto token=m_token; cancel();
        emit failed(token,QStringLiteral("YouTube source resolution timed out. Try the website player."));
    });
}
FeriaYouTubeSource::~FeriaYouTubeSource() { cancel(); }
QUrl FeriaYouTubeSource::watchUrl(const QUrl &url) {
    if (!FeriaNativePlayer::allowsSource(url)) return {};
    const auto host=url.host().toLower();
    QString id;
    if (host=="youtu.be") id=url.path().mid(1);
    else if (QStringList{"youtube.com","www.youtube.com","m.youtube.com","music.youtube.com"}.contains(host)) {
        if(url.path()=="/watch") id=QUrlQuery(url).queryItemValue("v");
        else if(url.path().startsWith("/shorts/") || url.path().startsWith("/live/")) id=url.path().section('/',2,2);
    }
    if (!QRegularExpression("^[A-Za-z0-9_-]{11}$").match(id).hasMatch()) return {};
    return QUrl("https://www.youtube.com/watch?v="+id);
}
QVariantMap FeriaYouTubeSource::candidate(const QByteArray &json,const QUrl &watch) {
    const auto data=QJsonDocument::fromJson(json).object();
    if (data.isEmpty() || data.value("has_drm").toBool()
            || data.value("id").toString()!=QUrlQuery(watch).queryItemValue("v")) return {};
    QString video=data.value("url").toString(), audio;
    QVariantMap headers=data.value("http_headers").toObject().toVariantMap();
    for(const auto &value:data.value("requested_formats").toArray()) {
        const auto format=value.toObject();
        if (format.value("has_drm").toBool()) return {};
        if (format.value("vcodec").toString()!=QStringLiteral("none")) {
            video=format.value("url").toString();
            headers=format.value("http_headers").toObject().toVariantMap();
        } else if (format.value("acodec").toString()!=QStringLiteral("none")) audio=format.value("url").toString();
    }
    auto allowed=[](const QString &source) {
        const QUrl url(source); const auto host=url.host().toLower();
        return FeriaNativePlayer::allowsSource(url) && url.scheme()=="https"
            && (host=="googlevideo.com" || host.endsWith(".googlevideo.com") || host=="www.youtube.com");
    };
    if(!allowed(video) || (!audio.isEmpty() && !allowed(audio))) return {};
    return {{"source",video},{"audioSource",audio},{"href",watch.toString()},
        {"title",data.value("title").toString().left(300)},{"headers",headers}};
}
bool FeriaYouTubeSource::resolve(const QUrl &url,const QString &token) {
    const auto watch=watchUrl(url);
    if (watch.isEmpty() || token.isEmpty()) return false;
    cancel();
    QString executable=qEnvironmentVariable("COLOSSEUM_YTDLP_PATH");
    if (executable.isEmpty()) executable=QStandardPaths::findExecutable("yt-dlp",{QCoreApplication::applicationDirPath(),QCoreApplication::applicationDirPath()+"/tools"});
    if (executable.isEmpty()) executable=QStandardPaths::findExecutable("yt-dlp");
    if (!QFileInfo(executable).isFile() || !QFileInfo(executable).isAbsolute()) {
        emit failed(token,QStringLiteral("YouTube native playback needs the local YouTube resolver. Use the website player until it is installed."));
        return false;
    }
    m_watch=watch; m_token=token; m_output.clear();
    auto *process=new QProcess(this); m_process=process;
    const auto current=[this,process,token] { return m_process==process && m_token==token; };
    connect(process,&QProcess::readyReadStandardOutput,this,[this,process,current,token] {
        if(!current())return;
        m_output+=process->readAllStandardOutput();
        if(m_output.size()>2*1024*1024) {cancel();emit failed(token,QStringLiteral("YouTube returned an unusable source response."));}
    });
    // Discard diagnostics: extractor errors may contain signed stream addresses.
    connect(process,&QProcess::readyReadStandardError,this,[process] {process->readAllStandardError();});
    connect(process,&QProcess::errorOccurred,this,[this,current,token](QProcess::ProcessError error) {
        if(current() && error==QProcess::FailedToStart) {cancel();emit failed(token,QStringLiteral("The local YouTube resolver could not start."));}
    });
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this,process,current,token](int code,QProcess::ExitStatus status) {
            if(!current())return;
            m_deadline.stop(); m_output+=process->readAllStandardOutput();
            auto result=code==0 && status==QProcess::NormalExit && m_output.size()<=2*1024*1024 ? candidate(m_output,m_watch) : QVariantMap{};
            m_process=nullptr; m_output.clear(); m_token.clear(); process->deleteLater();
            if(result.isEmpty())emit failed(token,QStringLiteral("This YouTube video could not be resolved. Use the website player."));
            else emit resolved(token,result);
        });
    QStringList args{"--ignore-config","--no-playlist","--no-cache-dir","--no-warnings","--skip-download","--dump-single-json",
        "--socket-timeout","15","--retries","1","--format","best[height<=720]/bestvideo[height<=720][vcodec^=avc1]+bestaudio/bestvideo[height<=720]+bestaudio"};
    const QString node=QStandardPaths::findExecutable("node");
    if(!node.isEmpty())args<<"--js-runtimes"<<"node:"+node;
    args<<"--"<<watch.toString();
    process->start(executable,args); m_deadline.start(); return true;
}
void FeriaYouTubeSource::cancel() {
    m_deadline.stop();
    auto *process=m_process.data(); m_process=nullptr;
    m_token.clear(); m_watch={}; m_output.clear();
    if(process) {
        disconnect(process,nullptr,this,nullptr);
        if(process->state()==QProcess::NotRunning) process->deleteLater();
        else {
            // Cancellation must not destroy a still-running child or wait on
            // the GUI thread. Its completion owns the deferred deletion.
            process->setParent(nullptr);
            connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,&QObject::deleteLater);
            process->kill();
        }
    }
}
