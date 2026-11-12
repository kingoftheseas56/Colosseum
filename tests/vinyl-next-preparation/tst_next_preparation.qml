import QtQuick
import QtTest
import Colosseum.Vinyl 1.0
TestCase {
 id: test; name: "VinylNextPreparation"; when: windowShown
 property var session; property var source; property var pending; property int plays: 0
 property var cancelled: []; property string audioUrl
 Component { id: sourceFactory; VideoTestSource {} }
 Component { id: sessionFactory; VinylBackendSession {} }
 function row(id) {
  var track={id:id,videoId:"abcdefghijk",title:id,available:true,seconds:120};
  return {trackId:id,track:track,discIndex:0,trackIndex:0,seconds:120,
   edition:{id:"edition:"+id,origin:"youtube-music",discs:[[track]],sourceAlbum:{tracks:[track]}}};
 }
 function init() {
  pending=null;plays=0;cancelled=[];
  var path=BridgeFixtures.createIdentityAudio();BridgeFixtures.changeIdentityAudio(path,120*8000);
  audioUrl="file:///"+path.replace(/\\/g,"/");
  source=sourceFactory.createObject(test);
  source.cancelled.connect(function(lane){cancelled.push(lane);});
  source.requested.connect(function(lane,id,operation,value,options){
   if(lane==="play"){plays++;Qt.callLater(function(){source.reply(lane,id,{trackId:value,url:audioUrl,provider:"youtube"},"");});}
   if(lane==="prepare-next"){compare(operation,"resolve");compare(options.source,"youtube");pending={id:id,trackId:value};}
  });
  session=sessionFactory.createObject(test,{source:source,surface:test,profileScope:"guest:next-preparation",nextPreparationEnabled:true});
  verify(session.replaceRows([row("one"),row("two"),row("three")],0,false));
  tryCompare(session,"loaded",true,10000);
 }
 function cleanup(){session.shutdown();session.destroy();source.destroy();wait(0);}
 function awaitPreparation(){tryVerify(function(){return pending!==null;},5000);}
 function resource(expire) {return {trackId:pending.trackId,videoId:"abcdefghijk",provider:"youtube",url:"https://media.googlevideo.com/videoplayback?expire="+expire};}
 function ready(){awaitPreparation();source.reply("prepare-next",pending.id,resource(Math.floor(Date.now()/1000)+3600),"");tryVerify(function(){return session.preparationState.ready;});}
 function test_unavailable_target_not_prepared(){session.suspend();var unavailable=row("unavailable");unavailable.track.available=false;verify(session.replaceRows([row("one"),unavailable],0,false));pending=null;tryCompare(session,"loaded",true);wait(500);compare(pending,null);var count=plays;session.next(1);tryVerify(function(){return session.error.length>0;});compare(plays,count);compare(session.preparationState.hits,0);}
 function test_automatic_one_slot_no_facts() {
  session.beginListeningFacts();var facts=session.preparationState.listenEvents;verify(facts>0);awaitPreparation();var player=session.player,occ=session.rows[0].queueId;
  compare(session.preparationState.starts,1);wait(400);compare(session.preparationState.starts,1);
  compare(session.player,player);compare(session.rows[session.activeIndex].queueId,occ);compare(plays,1);
  compare(session.preparationState.listenEvents,facts);compare(session.preparationState.listeningMs,0);
 }
 function test_ready_expires_before_next(){awaitPreparation();source.reply("prepare-next",pending.id,resource(Math.floor(Date.now()/1000)+122),"");tryVerify(function(){return session.preparationState.ready;});wait(2300);session.next(1);tryCompare(test,"plays",2);compare(session.preparationState.hits,0);}
 function test_refused_ready_once_fresh_resolve(){ready();session.next(1);tryCompare(test,"plays",2,25000);tryCompare(session,"loaded",true);compare(session.preparationState.hits,1);wait(300);compare(plays,2);}
 function test_ready_hit_has_no_second_resolve(){ready();session.next(1);compare(session.preparationState.hits,1);compare(plays,1);session.suspend();}
 function test_fixed_early_skip_cancels_and_resolves(){awaitPreparation();wait(20);session.next(1);tryCompare(test,"plays",2);verify(cancelled.indexOf("prepare-next")>=0);compare(session.preparationState.hits,0);}
 function test_reorder_rejects_stale(){awaitPreparation();var old=pending;session.moveQueue(1,2);source.reply("prepare-next",old.id,{trackId:old.trackId,videoId:"abcdefghijk",provider:"youtube",url:"https://media.googlevideo.com/videoplayback?expire="+(Math.floor(Date.now()/1000)+3600)},"");wait(100);verify(!session.preparationState.ready);compare(session.preparationState.hits,0);}
 function test_suspend_profile_reject_stale_data(){return [{tag:"suspend",profile:false},{tag:"profile",profile:true}];}
 function test_suspend_profile_reject_stale(data){awaitPreparation();var old=pending,r=resource(Math.floor(Date.now()/1000)+3600);if(data.profile)session.profileScope="guest:other";else session.suspend();source.reply("prepare-next",old.id,r,"");wait(100);verify(!session.preparationState.ready);}
 function test_invalid_falls_back_data(){return [{tag:"expired",kind:0},{tag:"identity",kind:1},{tag:"headers",kind:2}];}
 function test_invalid_falls_back(data){awaitPreparation();var r=resource(Math.floor(Date.now()/1000)+(data.kind===0?60:3600));if(data.kind===1)r.videoId="wrongwrong1";if(data.kind===2)r.headers={"Bad\nHeader":"bad"};source.reply("prepare-next",pending.id,r,"");tryVerify(function(){return session.preparationState.status==="failed";});session.next(1);tryCompare(test,"plays",2);compare(session.preparationState.hits,0);}
 function test_failure_keeps_audio(){awaitPreparation();var player=session.player,occ=session.rows[session.activeIndex].queueId;source.reply("prepare-next",pending.id,{},"provider failure");tryVerify(function(){return session.preparationState.status==="failed";});compare(session.player,player);compare(session.rows[session.activeIndex].queueId,occ);verify(session.loaded);compare(session.error,"");}
}
