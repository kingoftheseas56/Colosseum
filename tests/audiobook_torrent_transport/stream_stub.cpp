#include "../../native/player/streamserver.h"
QString stubStreamUrl;
StreamServer::StreamServer(QObject* parent):QObject(parent) {}
StreamServer::~StreamServer()=default;
void StreamServer::prefetch(const QString& hash,int idx) { emit fetchReady(stubStreamUrl,hash,idx); }
QString StreamServer::streamUrl(const QString&,int) const { return stubStreamUrl; }
void StreamServer::play(const QString&,int) {}
void StreamServer::warmSource(const QString&,int) {}
void StreamServer::warmUp() {}
void StreamServer::watchStats(const QString&,int) {}
void StreamServer::unwatchStats() {}
void StreamServer::stopDemand() {}
void StreamServer::requestManifest(const QString&,const QString&) {}
void StreamServer::requestMagnetManifest(const QString&,const QString&,const QString&) {}
void StreamServer::requestFile(const QString&,const QString&,int) {}
void StreamServer::cancelRequest(const QString&) {}
