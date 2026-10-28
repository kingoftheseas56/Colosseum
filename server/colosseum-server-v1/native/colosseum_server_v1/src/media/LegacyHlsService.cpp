#include "server1/media/LegacyHlsService.h"
#include "server1/media/Probe.h"
#include "server1/media/LegacyMediaIndex.h"
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QQueue>
#include <utility>
#include <QUrl>
#include <cmath>
#include <cstring>
#include <limits>
namespace server1::media {
namespace {
QString str(QJsonValue v){if(v.isUndefined())return "undefined";if(v.isNull())return "null";if(v.isBool())return v.toBool()?"true":"false";if(v.isDouble())return QString::number(v.toDouble(),'g',16);if(v.isArray()){QStringList a;for(auto x:v.toArray())a<< (x.isNull()?QString():str(x));return a.join(',');}return v.toString();}
bool truth(QJsonValue v){return !v.isUndefined()&&!v.isNull()&&(!v.isBool()||v.toBool())&&(!v.isString()||!v.toString().isEmpty())&&(!v.isDouble()||v.toDouble()!=0);}
double num(QJsonValue v){
 if(v.isUndefined()||v.isObject())return NAN;
 if(v.isNull())return 0;
 if(v.isDouble())return v.toDouble();
 if(v.isBool())return v.toBool();
 const auto s=str(v).trimmed();
 if(s.isEmpty())return 0;
 if(s=="Infinity"||s=="+Infinity")return std::numeric_limits<double>::infinity();
 if(s=="-Infinity")return -std::numeric_limits<double>::infinity();
 // Global isNaN uses Number, not parseInt. Unsigned radix-prefixed strings
 // choose transcoding even though the later decimal parseInt returns zero.
 if(s.size()>2&&s[0]=='0'){
  const auto prefix=s[1].toLower();const int radix=prefix=='x'?16:prefix=='b'?2:prefix=='o'?8:0;
  if(radix){double result=0;for(qsizetype i=2;i<s.size();++i){const auto c=s[i].toLower();const int digit=c>='0'&&c<='9'?c.unicode()-'0':c>='a'&&c<='f'?c.unicode()-'a'+10:-1;if(digit<0||digit>=radix)return NAN;result=result*radix+digit;}return result;}
 }
 // QString accepts "inf" while JavaScript does not. Validate the decimal
 // grammar before conversion. Qt reports range errors for both overflow and
 // underflow, but JavaScript keeps the resulting Infinity or zero.
 static const QRegularExpression decimal("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$");
 if(!decimal.match(s).hasMatch())return NAN;
 return s.toDouble();
}
double integer(QJsonValue v){auto m=QRegularExpression("^\\s*([+-]?[0-9]+)").match(str(v));return m.hasMatch()?m.captured(1).toDouble():NAN;}
QString secs(double n){return QString::number(n/1000,'f',3);}
QString nstr(double n){return std::isfinite(n)?QString::number(n,'g',16):"NaN";}
LegacyResponse error(QString s,int code=500){return {code,{{"cache-control","no-cache"}},s.toUtf8(),{}, {}};}
bool transmux(QJsonObject s){auto type=s["codec_type"].toString(),name=s["codec_name"].toString().toLower();return type=="video"?name.contains("h264"):type=="audio"&&name.contains("aac");}
QStringList videoArgs(){return {"-c:v","libx264","-pix_fmt","yuv420p","-preset","veryfast","-tune","zerolatency"};}
QStringList audioArgs(){return {"-c:a","aac","-strict","experimental","-ac","2"};}
QStringList endArgs(){return {"-copyts","-mpegts_copyts","1","-f","mpegts","-threads","0","pipe:1"};}
QString language(QString code){static const QJsonObject names = []{QJsonObject o;
o["abk"]="аҧсуа бызшәа";
o["aar"]="Afaraf";
o["afr"]="Afrikaans";
o["aka"]="Akan";
o["sqi"]="gjuha shqipe";
o["amh"]="አማርኛ";
o["ara"]="العربية";
o["arg"]="aragonés";
o["hye"]="Հայերեն";
o["asm"]="অসমীয়া";
o["ava"]="авар мацӀ";
o["ave"]="avesta";
o["aym"]="aymar aru";
o["aze"]="azərbaycan dili";
o["bam"]="bamanankan";
o["bak"]="башҡорт теле";
o["eus"]="euskara";
o["bel"]="беларуская мова";
o["ben"]="বাংলা";
o["bih"]="भोजपुरी";
o["bis"]="Bislama";
o["bos"]="bosanski jezik";
o["bre"]="brezhoneg";
o["bul"]="български език";
o["mya"]="ဗမာစာ";
o["cat"]="català";
o["cha"]="Chamoru";
o["che"]="нохчийн мотт";
o["nya"]="chiCheŵa";
o["zho"]="中文 (Zhōngwén)";
o["chv"]="чӑваш чӗлхи";
o["cor"]="Kernewek";
o["cos"]="corsu";
o["cre"]="ᓀᐦᐃᔭᐍᐏᐣ";
o["hrv"]="hrvatski jezik";
o["ces"]="čeština";
o["dan"]="dansk";
o["div"]="ދިވެހި";
o["nld"]="Nederlands";
o["dzo"]="རྫོང་ཁ";
o["eng"]="English";
o["epo"]="Esperanto";
o["est"]="eesti";
o["ewe"]="Eʋegbe";
o["fao"]="føroyskt";
o["fij"]="vosa Vakaviti";
o["fin"]="suomi";
o["fre"]="français";
o["ful"]="Fulfulde";
o["glg"]="galego";
o["kat"]="ქართული";
o["ger"]="Deutsch";
o["ell"]="ελληνικά";
o["grn"]="Avañe'ẽ";
o["guj"]="ગુજરાતી";
o["hat"]="Kreyòl ayisyen";
o["hau"]="Hausa";
o["heb"]="עברית";
o["her"]="Otjiherero";
o["hin"]="हिन्दी";
o["hmo"]="Hiri Motu";
o["hun"]="magyar";
o["ina"]="Interlingua";
o["ind"]="Bahasa Indonesia";
o["ile"]="Interlingue";
o["gle"]="Gaeilge";
o["ibo"]="Asụsụ Igbo";
o["ipk"]="Iñupiaq";
o["ido"]="Ido";
o["isl"]="Íslenska";
o["ita"]="italiano";
o["iku"]="ᐃᓄᒃᑎᑐᑦ";
o["jpn"]="日本語 (にほんご)";
o["jav"]="basa Jawa";
o["kal"]="kalaallisut";
o["kan"]="ಕನ್ನಡ";
o["kau"]="Kanuri";
o["kas"]="कश्मीरी";
o["kaz"]="қазақ тілі";
o["khm"]="ខ្មែរ";
o["kik"]="Gĩkũyũ";
o["kin"]="Ikinyarwanda";
o["kir"]="Кыргызча";
o["kom"]="коми кыв";
o["kon"]="KiKongo";
o["kor"]="한국어 (韓國語)";
o["kur"]="Kurdî";
o["kua"]="Kuanyama";
o["lat"]="latine";
o["ltz"]="Lëtzebuergesch";
o["lug"]="Luganda";
o["lim"]="Limburgs";
o["lin"]="Lingála";
o["lao"]="ພາສາລາວ";
o["lit"]="lietuvių kalba";
o["lub"]="Tshiluba";
o["lav"]="latviešu valoda";
o["glv"]="Gaelg";
o["mkd"]="македонски јазик";
o["mlg"]="fiteny malagasy";
o["msa"]="bahasa Melayu";
o["mal"]="മലയാളം";
o["mlt"]="Malti";
o["mri"]="te reo Māori";
o["mar"]="मराठी";
o["mah"]="Kajin M̧ajeļ";
o["mon"]="монгол";
o["nau"]="Ekakairũ Naoero";
o["nav"]="Diné bizaad";
o["nob"]="Norsk bokmål";
o["nde"]="isiNdebele";
o["nep"]="नेपाली";
o["ndo"]="Owambo";
o["nno"]="Norsk nynorsk";
o["nor"]="Norsk";
o["iii"]="ꆈꌠ꒿ Nuosuhxop";
o["nbl"]="isiNdebele";
o["oci"]="occitan";
o["oji"]="ᐊᓂᔑᓈᐯᒧᐎᓐ";
o["chu"]="ѩзыкъ словѣньскъ";
o["orm"]="Afaan Oromoo";
o["ori"]="ଓଡ଼ିଆ";
o["oss"]="ирон æвзаг";
o["pan"]="ਪੰਜਾਬੀ";
o["pli"]="पाऴि";
o["fas"]="فارسی";
o["pol"]="język polski";
o["pus"]="پښتو";
o["por"]="português";
o["pob"]="português Brazil";
o["que"]="Runa Simi";
o["roh"]="rumantsch grischun";
o["run"]="Ikirundi";
o["ron"]="limba română";
o["rus"]="русский язык";
o["san"]="संस्कृतम्";
o["srd"]="sardu";
o["snd"]="सिन्धी";
o["sme"]="Davvisámegiella";
o["smo"]="gagana fa'a Samoa";
o["sag"]="yângâ tî sängö";
o["srp"]="српски језик";
o["gla"]="Gàidhlig";
o["sna"]="chiShona";
o["sin"]="සිංහල";
o["slk"]="slovenčina";
o["slv"]="slovenski jezik";
o["som"]="Soomaaliga";
o["sot"]="Sesotho";
o["spa"]="español";
o["sun"]="Basa Sunda";
o["swa"]="Kiswahili";
o["ssw"]="SiSwati";
o["swe"]="Svenska";
o["tam"]="தமிழ்";
o["tel"]="తెలుగు";
o["tgk"]="тоҷикӣ";
o["tha"]="ไทย";
o["tir"]="ትግርኛ";
o["bod"]="བོད་ཡིག";
o["tuk"]="Türkmen";
o["tgl"]="Wikang Tagalog";
o["tsn"]="Setswana";
o["ton"]="faka Tonga";
o["tur"]="Türkçe";
o["tso"]="Xitsonga";
o["tat"]="татар теле";
o["twi"]="Twi";
o["tah"]="Reo Tahiti";
o["uig"]="Uyƣurqə";
o["ukr"]="українська мова";
o["urd"]="اردو";
o["uzb"]="O'zbek";
o["ven"]="Tshivenḓa";
o["vie"]="Tiếng Việt";
o["vol"]="Volapük";
o["wln"]="walon";
o["cym"]="Cymraeg";
o["wol"]="Wollof";
o["fry"]="Frysk";
o["xho"]="isiXhosa";
o["yid"]="ייִדיש";
o["yor"]="Yorùbá";
o["zha"]="Saɯ cueŋƅ";
o["zul"]="isiZulu";
return o;}();return names[code].toString();}
}
LegacyResponse LegacyHlsService::render(QString action,QJsonObject inst,QJsonObject in,QJsonArray subtitles,bool hasSplitter){
 const auto p=in["params"].toObject(),q=in["query"].toObject();QString url=in["url"].toString(),from=str(p["from"]);auto streams=inst["streams"].toArray();auto qualities=inst["qualities"].toArray();auto vs=inst["videoStream"].toObject();double duration=inst["duration"].toDouble();
 LegacyResponse r;QStringList lines;auto playlist=[&]{r.body=(lines.join('\n')+'\n').toUtf8();r.headers={{"content-type","application/vnd.apple.mpegurl"},{"content-length",r.body.size()}};return r;};
 bool subs=truth(q["waitForSubs"])&&(!subtitles.isEmpty()||in["_subtitlesPresent"].toBool());
 if(action=="master"||action=="multi"){
  QString query=url.contains('?')?url.mid(url.indexOf('?')):QString();QString base=url.left(url.lastIndexOf('/')+1);lines<<"#EXTM3U"<<"#EXT-X-VERSION:4";
  if(subs)for(auto v:subtitles){auto s=v.toObject();if(truth(s["lang"]))lines<<"#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=\""+(truth(s["langName"])?str(s["langName"]):QString("undefined"))+"\",DEFAULT=YES,AUTOSELECT=YES,FORCED=NO,LANGUAGE=\""+str(s["lang"])+"\",URI=\""+base+"/subs-"+str(s["lang"])+".m3u8"+query+"\"";}
  if(action=="multi"||truth(p["hlsMultiStream"]))for(auto v:streams){auto s=v.toObject();if(s["codec_type"]=="audio"){QString a="#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",";if(truth(s["lang"]))a+="LANGUAGE=\""+str(s["lang"])+"\",AUTOSELECT=YES,";if(truth(s["default"]))a+="DEFAULT=YES,";auto name=language(s["lang"].toString());if(name.isEmpty())name=truth(s["lang"])?str(s["lang"]):"Audio "+str(s["stream"]);lines<<a+"NAME=\""+name+"\",URI=\"stream-"+str(s["stream"])+".m3u8"+query+"\"";}if(s["codec_type"]=="video")lines<<"#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH="+str(inst["bitrate"])+",AUDIO=\"audio\""+(subs?",SUBTITLES=\"subs\"":"")<<base+"stream-"+str(s["stream"])+".m3u8"+query;}
  else {QStringList variants;int i=0;bool h264=false;for(auto s:streams)h264|=s.toObject()["codec_name"]=="h264";auto entry=[&](QString b,QString qual){variants<<"#EXT-X-STREAM-INF:PROGRAM-ID="+QString::number(i++)+",BANDWIDTH="+b+(subs?",SUBTITLES=\"subs\"":"")+"\n"+base+"stream-q-"+qual+".m3u8"+query;};if(hasSplitter&&inst["segmentsByFrame"].isArray()&&h264)entry(str(inst["bitrate"]),"o");auto size=vs["size"].toArray();for(auto v:qualities){double h=v.toDouble(),b=1000*h*h*num(size.at(0))/num(size.at(1))*(truth(vs["fps"])?num(vs["fps"]):24)*.11/1024;entry(nstr(std::floor(b+.5)),str(v));}if(!truth(p["adaptiveBitrate"])&&variants.size()>1)variants=variants.mid(0,1);lines+=variants;}return playlist();
 }
 if(action=="playlist"){
  double quality=integer(p["quality"]);if(!qualities.contains(quality)&&p["quality"]!="o")return error("no such quality",404);
  auto segs=inst[std::isnan(num(p["quality"]))?"segmentsByFrame":"segmentsUniform"].toArray();QString prefix;if(p.contains("stream")||p.contains("quality"))prefix=url.mid(url.lastIndexOf('/')+1).section('.',0,0)+"/";
  double max=0;for(auto s:segs)max=std::max(max,s.toObject()["duration"].toDouble());lines<<"#EXTM3U"<<"#EXT-X-VERSION:4"<<"#EXT-X-TARGETDURATION:"+nstr(std::ceil(max/1000))<<"#EXT-X-MEDIA-SEQUENCE:0"<<"#EXT-X-PLAYLIST-TYPE:VOD";for(int i=0;i<segs.size();i++){if(truth(q["xdiscontinuity"]))lines<<"#EXT-X-DISCONTINUITY";lines<<"#EXTINF:"+secs(segs[i].toObject()["duration"].toDouble())+","<<prefix+QString::number(i)+".ts"+in["search"].toString();}lines<<"#EXT-X-ENDLIST";return playlist();
 }
 if(action=="subtitle"){
  QJsonObject sub;for(auto s:subtitles)if(s.toObject()["lang"]==p["lang"]){sub=s.toObject();break;}if(sub.isEmpty())return r;
  lines<<"#EXTM3U"<<"#EXT-X-VERSION:4"<<"#EXT-X-TARGETDURATION:"+nstr(std::ceil(duration/1000))<<"#EXT-X-MEDIA-SEQUENCE:1"<<"#EXT-X-PLAYLIST-TYPE:VOD"<<"#EXTINF:"+secs(duration)+","<<"http://"+str(in["headers"].toObject()["host"])+"/subtitles.vtt?from="+QString::fromLatin1(QUrl::toPercentEncoding(str(sub["url"]),"!'()*"))<<"#EXT-X-ENDLIST";return playlist();
 }
 r.program="ffmpeg";
 if(action=="thumb"){r.headers["content-type"]="image/jpg";r.arguments={"-ss",std::isnan(num(q["at"]))?nstr(std::floor(duration/2000+.5)):str(q["at"]),"-i",from,"-r","1","-vframes","1","-f","image2","-vcodec","mjpeg","pipe:1"};return r;}
 if(action=="dlna"){
  bool vt=true,at=true;for(auto v:streams){auto s=v.toObject();if(s["codec_type"]=="video")vt&=transmux(s);if(s["codec_type"]=="audio")at&=transmux(s);}double start=integer(p["at"]);r.arguments={"-ss",secs(std::isnan(start)?0:start),"-i",from,"-c:v",vt?"copy":"libx264","-c:a",at?"copy":"aac"};if(vt)for(auto v:streams){auto s=v.toObject();if(s["codec_type"]=="video"){auto codec=s["codec_name"].toString();if(codec=="h264"||codec=="hevc")r.arguments<<"-vbsf"<<codec+"_mp4toannexb";break;}}r.arguments<<"-strict"<<"-2"<<"-loglevel"<<"error"<<"-tune"<<"zerolatency"<<"-f"<<"mpegts"<<"pipe:1";r.headers={{"content-type","video/vnd.dlna.mpeg-tts"},{"transfermode.dlna.org","Streaming"},{"contentfeatures.dlna.org","DLNA.ORG_PN=AVC_TS_BL_CIF15_AAC_540;DLNA.ORG_FLAGS=ED100000000000000000000000000000"}};return r;
 }
 if(action=="segment"||action=="transcode"){
  auto quality=p["quality"];if(!truth(quality))quality=inst["segmentsByFrame"].isArray()?QJsonValue("o"):QJsonValue(720);
  double index=integer(p["seg"]);const auto streamKey=str(p["stream"]);bool sok=false;const int si=streamKey.toInt(&sok);
  // JS array property lookup does not canonicalize "01", "+1", or " 1 ".
  QJsonObject selected=(sok&&si>=0&&si<streams.size()&&QString::number(si)==streamKey)?streams[si].toObject():QJsonObject();
  bool trans=action=="transcode"||!std::isnan(num(quality));if(!trans){auto segs=inst["segmentsByFrame"].toArray();if(!std::isfinite(index)||index<0||index>=segs.size())return error("segment does not exist in segmentsByFrame",404);if(!selected.isEmpty())trans=selected["codec_type"]=="video"&&!transmux(selected);else for(auto s:streams)trans|=s.toObject()["codec_type"]=="video"&&!transmux(s.toObject());if(!trans){auto seg=segs[int(index)].toObject();r.program="ffsplit";r.arguments={from,str(seg["at"]),str(seg["duration"]),selected.isEmpty()?"-1":str(selected["stream"])};r.headers={{"content-type","video/mp2t"},{"x-hls-flow","splitter"}};return r;}}
  auto segs=inst["segmentsUniform"].toArray();if(!std::isfinite(index)||index<0||index>=segs.size())return error("segment does not exist in segmentsUniform",404);auto seg=segs[int(index)].toObject();double qual=integer(quality);if(std::isfinite(qual)&&qual!=0&&!qualities.contains(qual))return error("no such quality",404);
  r.arguments={"-ss",secs(seg["at"].toDouble()),"-t",secs(seg["duration"].toDouble()),"-i",from,"-af","aselect=gt(n\\,1)"};if(std::isfinite(qual)&&qual!=0&&vs["size"].toArray().at(1).toDouble()-qual>100)r.arguments<<"-vf"<<"scale=-2:"+str(quality);
  if(!selected.isEmpty()){r.arguments<<"-map"<<"0:"+str(selected["stream"]);r.arguments+=(selected["codec_type"]=="video"?videoArgs():audioArgs());}else r.arguments+=videoArgs()+audioArgs();r.arguments+=endArgs();r.headers={{"content-type","video/mp2t"},{"x-hls-flow","transcoder"}};return r;
 }
 return error("unknown legacy action",404);
}
struct LegacyOperation::Impl {
 ProcessPort port;
 std::unique_ptr<LegacyProbe> probe;
 quint64 probeId=0,processId=0;
 QPointer<QObject> retrieval;
 QByteArray buffer;
 bool finished=false,canceled=false,paused=false;
 std::function<void()> release;
};
LegacyOperation::LegacyOperation():impl(new Impl){}
LegacyOperation::~LegacyOperation(){cancel();}
qsizetype LegacyOperation::bufferedBytes() const{return impl->buffer.size();}
bool LegacyOperation::running() const{return impl->port.activeCount()!=0;}
void LegacyOperation::cancel(){if(impl->canceled)return;impl->canceled=true;wake={};if(impl->release){auto release=std::exchange(impl->release,{});release();}if(impl->retrieval)delete impl->retrieval;if(impl->probe&&impl->probeId)impl->probe->cancel(impl->probeId);if(impl->processId)(void)impl->port.cancel(impl->processId);}
qint64 LegacyOperation::read(char* dest,qint64 cap){if(impl->canceled)return -1;if(impl->buffer.isEmpty())return impl->finished?0:-2;auto n=std::min<qint64>(cap,impl->buffer.size());memcpy(dest,impl->buffer.constData(),size_t(n));impl->buffer.remove(0,n);if(impl->paused&&impl->buffer.size()<65536){impl->paused=false;(void)impl->port.resumeOutput(impl->processId);}return n;}
struct LegacyHlsService::Impl {
 ExecutablePaths paths;
 QMap<QString,QJsonObject> cache;
 QMap<QString,QJsonArray> subs;
 QVector<std::weak_ptr<LegacyOperation>> operations;
 int active=0;
 QQueue<std::function<bool()>> queue;
 void pump(){while(active<8&&!queue.isEmpty()){auto start=queue.dequeue();if(start())++active;}}
 Impl(ExecutablePaths p):paths(std::move(p)){}
};
LegacyHlsService::LegacyHlsService(ExecutablePaths paths,QObject* parent):QObject(parent),impl(new Impl(std::move(paths))){}
LegacyHlsService::~LegacyHlsService(){for(auto weak:impl->operations)if(auto op=weak.lock())op->cancel();}
void LegacyHlsService::receiveSubtitles(QString id,QJsonArray subs){impl->subs[id]=std::move(subs);}
std::shared_ptr<LegacyOperation> LegacyHlsService::request(QString action,QJsonObject input,Callback callback){
 auto op=std::make_shared<LegacyOperation>();impl->operations.erase(std::remove_if(impl->operations.begin(),impl->operations.end(),[](auto w){return w.expired();}),impl->operations.end());impl->operations.append(op);std::weak_ptr<LegacyOperation> weak=op;QPointer<LegacyHlsService> guard(this);auto from=str(input["params"].toObject()["from"]);
 auto deliver=[weak,guard,action,input,callback](QJsonObject instance,QJsonObject err){auto o=weak.lock();if(!o||o->impl->canceled||!guard)return;
  if(action=="probe"){LegacyResponse r;r.status=err.isEmpty()?200:500;r.headers["content-type"]="application/json";
   // Module 305 returns undefined only for the early missing-executable error;
   // its runtime failure/timeout callback supplies null to the /probe JSON body.
   r.body=err.isEmpty()?QJsonDocument(instance).toJson(QJsonDocument::Compact)
      :(guard->impl->paths.ffmpeg.isEmpty()?QByteArray():QByteArray("null"));callback(r);return;}
  if(!err.isEmpty()){callback(error(err["message"].toString(),err["httpCode"].toInt(500)));return;}
  auto enriched=input;auto subId=input["query"].toObject()["waitForSubs"].toString();enriched["_subtitlesPresent"]=guard->impl->subs.contains(subId);auto response=render(action,instance,enriched,guard->impl->subs.value(subId),!guard->impl->paths.ffsplit.isEmpty());
  if(response.program.isEmpty()){callback(response);return;}
  QString exe=response.program=="ffsplit"?guard->impl->paths.ffsplit:guard->impl->paths.ffmpeg;if(exe.isEmpty()){callback(error(response.program=="ffmpeg"?"no ffmpeg found":"no ffsplit found"));return;}
  ProcessSpec spec(exe,response.arguments);spec.backpressuredOutput=true;auto sent=std::make_shared<bool>(false);ProcessCallbacks cb;
  cb.onStdout=[weak,response,callback,sent](const QByteArray& b){auto o=weak.lock();if(!o||o->impl->canceled)return;o->impl->buffer+=b;if(!*sent){*sent=true;callback(response);}if(o->impl->buffer.size()>=131072){o->impl->paused=true;(void)o->impl->port.pauseOutput(o->impl->processId);}if(o->wake)o->wake();};
  cb.onFinished=[weak,response,callback,sent](const ProcessResult& result) mutable{auto o=weak.lock();if(!o)return;if(o->impl->release){auto release=std::exchange(o->impl->release,{});release();}if(o->impl->canceled)return;o->impl->finished=true;if(!*sent){*sent=true;if(!result.started)callback(error(result.errorString));else{response.status=result.exitCode==0?200:500;callback(response);}}if(o->wake)o->wake();};
  guard->impl->queue.enqueue([weak,guard,spec,cb=std::move(cb)]()mutable{auto o=weak.lock();if(!o||o->impl->canceled||!guard)return false;
   o->impl->release=[guard]{if(!guard)return;--guard->impl->active;QTimer::singleShot(0,guard,[guard]{if(guard)guard->impl->pump();});};
   o->impl->processId=o->impl->port.start(spec,std::move(cb));return true;});guard->impl->pump();
 };
 auto prepared=[weak,guard,from,deliver](QJsonObject instance,QJsonObject err){auto o=weak.lock();if(!o||o->impl->canceled||!guard)return;if(err.isEmpty())guard->impl->cache[from]=instance;deliver(instance,err);};
 auto probed=[weak,guard,action,from,deliver,prepared](const ProbeResult& result){auto o=weak.lock();if(!o||o->impl->canceled||!guard)return;if(action=="thumb"||action=="probe"||!result.ok()){deliver(result.value,result.error);return;}
  auto instance=result.value;auto streams=instance["streams"].toArray();QJsonObject video;for(auto v:streams)if(v.toObject()["codec_type"]=="video"){video=v.toObject();break;}if(video.isEmpty()){deliver({},{{"message","no video stream found"},{"httpCode",415}});return;}double duration=instance["duration"].toDouble();if(!duration){deliver({},{{"message","no video stream duration"},{"httpCode",415}});return;}
  instance["from"]=from;instance["videoStream"]=video;QJsonArray qualities,segs;double height=video["size"].toArray().at(1).toDouble();for(int h:{320,480,720})if(h<=height)qualities.append(h);if(qualities.isEmpty())qualities.append(height);instance["qualities"]=qualities;
  for(double at=0;at<duration;at+=6000)segs.append(QJsonObject{{"at",at},{"duration",std::min(6000.,duration-at)}});instance["segmentsUniform"]=segs;
  auto container=instance["container"].toString();if(container!="mp4"&&container!="matroska"){prepared(instance,{});return;}
  QUrl url(from);if(url.scheme().isEmpty())url=QUrl::fromLocalFile(from);auto done=std::make_shared<bool>(false);
  auto complete=[weak,prepared,instance,container,done](SamplesResult samples)mutable{auto o=weak.lock();if(!o||o->impl->canceled||*done)return;*done=true;if(!samples.available){prepared({},{{"message",samples.error},{"httpCode",500}});return;}QJsonObject track;for(auto t:samples.tracks)if(t.toObject()["type"]=="video"){track=t.toObject();if(container=="mp4")break;}auto raw=track["samples"].toArray();double scale=track["timescale"].toDouble(1000);QVector<double> keys;for(int i=0;i<raw.size();i++){auto s=raw[i].toObject();if(s["key"].toBool()){double d=container=="mp4"?s["dts"].toDouble():s["pts"].toDouble();keys<<d;}}if(container=="mp4"&&!keys.isEmpty()&&keys.first()!=0)keys.prepend(0);QJsonArray segs;for(int i=0;i<keys.size();i++){double d=(i+1<keys.size()?keys[i+1]:instance["duration"].toDouble())-keys[i];if(!segs.isEmpty()&&segs.last().toObject()["duration"].toDouble()<1){auto last=segs.last().toObject();last["duration"]=last["duration"].toDouble()+d;segs.replace(segs.size()-1,last);}else segs.append(QJsonObject{{"at",std::floor(keys[i])},{"dts",keys[i]},{"duration",std::floor(d)}});}instance["segmentsByFrame"]=segs;prepared(instance,{});};
  o->impl->retrieval=container=="mp4"?retrieveLegacyMp4Samples(url,{},complete,o.get()):retrieveLegacyMkvSamples(url,{},complete,o.get());QTimer::singleShot(10000,o.get(),[weak,done,prepared]{auto o=weak.lock();if(!o||o->impl->canceled||*done)return;*done=true;if(o->impl->retrieval)delete o->impl->retrieval;prepared({},{{"message","retrieveKeyframes timed out"},{"httpCode",500}});});
 };
 if(action!="probe"&&action!="thumb"&&impl->cache.contains(from)){auto instance=impl->cache[from];QTimer::singleShot(0,op.get(),[deliver,instance]{deliver(instance,{});});}
 else{op->impl->probe=std::make_unique<LegacyProbe>(impl->paths.ffmpeg);op->impl->probeId=op->impl->probe->start(from,probed);}return op;
}
} // namespace server1::media
