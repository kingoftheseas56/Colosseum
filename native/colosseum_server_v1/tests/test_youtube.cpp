#include "server1/youtube/YouTubeResolver.h"
#include "server1/http/YouTubeRoutes.h"
#include "server1/http/HttpContract.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QTimer>
#include <iostream>
#include <stdexcept>
using namespace server1;
void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{check(argc==2,"fixtures required");QFile file(QString::fromLocal8Bit(argv[1]));check(file.open(QIODevice::ReadOnly),"missing fixture");const auto rows=QJsonDocument::fromJson(file.readAll()).array();int count=0;
 for(const auto &value:rows){const auto row=value.toObject(),pages=row.value("pages").toObject();int requests=0;
 youtube::YouTubeResolver resolver(nullptr,[&](const QUrl &url,youtube::YouTubeResolver::PageCallback cb){++requests;auto key=url.toString(QUrl::FullyEncoded);if(key.contains("eurl="))key.replace("https://youtube.googleapis.com/v/","https%3A%2F%2Fyoutube.googleapis.com%2Fv%2F");QTimer::singleShot(0,&app,[cb=std::move(cb),pages,key]{cb({pages.value(key).toString().toUtf8(),pages.contains(key)?QString{}:"Missing fixture: "+key,200});});});
 youtube::Result result;bool done=false;resolver.resolve(row.value("id").toString(),[&](youtube::Result r){result=std::move(r);done=true;});QElapsedTimer timeout;timeout.start();while(!done&&timeout.elapsed()<3000)app.processEvents(QEventLoop::AllEvents,50);check(done,"resolver did not finish");
 const QJsonObject actual{{"format",result.format},{"error",result.error}};if(actual!=row.value("expected").toObject()){std::cerr<<row.value("name").toString().toStdString()<<"\nactual "<<QJsonDocument(actual).toJson().constData()<<"expected "<<QJsonDocument(row.value("expected").toObject()).toJson().constData();throw std::runtime_error("resolver differential mismatch");}
 const auto actions=row.value("actions").toArray();QStringList tokens;for(const auto &a:actions)tokens.append(a.toString());check(youtube::YouTubeResolver::extractActions(row.value("tokenScript").toString())==tokens,"extract action mismatch");check(youtube::YouTubeResolver::decipher(tokens,"abcdef")==row.value("deciphered").toString(),"decipher mismatch");
 const int initialRequests=requests;auto *router=server1_http_router_create();check(http::installYouTubeRoutes(router,resolver),"route install failed");
 for(bool json:{true,false}) {const auto target="/yt/"+row.value("id").toString()+(json?".json":"");if(row.value("id").toString().isEmpty())continue;auto *res=server1_http_router_dispatch(router,"GET",target.toUtf8().constData(),"");timeout.restart();while(server1_http_response_is_deferred(res)&&timeout.elapsed()<3000)app.processEvents(QEventLoop::AllEvents,50);check(!server1_http_response_is_deferred(res),"route unresolved");const auto expectedRoute=row.value("routes").toObject().value(json?"json":"redirect").toObject();check(server1_http_response_status(res)==expectedRoute.value("status").toInt(),"pinned route callback status mismatch");const bool success=result.error.isEmpty()&&!result.format.value("url").toString().isEmpty();check(server1_http_response_status(res)==(success?(json?200:301):(json?404:403)),"route status mismatch");const QByteArray body(server1_http_response_body(res),server1_http_response_body_size(res));if(json){check(QJsonDocument::fromJson(body).object()==(success?result.format:QJsonObject{{"err",result.error}}),"route JSON mismatch");check(QByteArray(server1_http_response_header(res,"Content-Type"))=="application/json","route type mismatch");}else{if(!body.isEmpty())std::cerr<<row.value("name").toString().toStdString()<<" redirect body: "<<body.constData()<<"\n";check(body.isEmpty(),"redirect body nonempty");if(success)check(QString::fromUtf8(server1_http_response_header(res,"Location"))==result.format.value("url").toString(),"redirect mismatch");}server1_http_response_destroy(res);}
 server1_http_router_destroy(router);if(result.error.isEmpty())check(requests==initialRequests,"successful result cache missed");++count;
 }
 // Safety stops do not retry alternate endpoints after consent/sign-in/age restrictions.
 for(const auto &body:QList<QByteArray>{"<title>Before you continue to YouTube</title>","<script>var ytInitialPlayerResponse={\"playabilityStatus\":{\"status\":\"LOGIN_REQUIRED\",\"reason\":\"Sign in to confirm your age\"}};</script><script>var ytInitialData={};</script>"}){
  int requests=0,callbacks=0;youtube::YouTubeResolver::PageCallback deliver;
  youtube::YouTubeResolver r(nullptr,[&](const QUrl &,auto cb){++requests;deliver=std::move(cb);});
  r.resolve("abcdefghijk",[&](auto result){check(!result.error.isEmpty(),"restriction missing");++callbacks;});
  r.resolve("abcdefghijk",[&](auto result){check(!result.error.isEmpty(),"coalesced restriction missing");++callbacks;});
  check(requests==1,"concurrent resolve not coalesced");deliver({body,{},200});check(callbacks==2&&requests==1,"restriction attempted fallback");
 }
 {youtube::YouTubeResolver::PageCallback deliver;youtube::YouTubeResolver r(nullptr,[&](const QUrl &,auto cb){deliver=std::move(cb);});auto *router=server1_http_router_create();check(http::installYouTubeRoutes(router,r),"install failed");auto *res=server1_http_router_dispatch(router,"GET","/yt/abcdefghijk.json","");server1_http_response_destroy(res);server1_http_router_destroy(router);deliver({"<title>Before you continue</title>",{},200});}
 // Destruction settles callers; no retained dead QObject or dangling deferred response.
 int destroyed=0;{youtube::YouTubeResolver r(nullptr,[](const QUrl &,auto){});r.resolve("abcdefghijk",[&](auto result){check(!result.error.isEmpty(),"destroyed resolver error missing");++destroyed;});}check(destroyed==1,"destruction did not settle callback");
 std::cout<<"PASS "<<count<<" YouTube fetched-page differentials and JSON/redirect pairs\n";return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
