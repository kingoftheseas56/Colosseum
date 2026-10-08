'use strict';
(async () => {
  const lab = await window.labReady;
  if (!lab.boot.selfTest || !lab.probe) return;
  const {invoke,probe,remote,adapter} = lab;
  const sleep = ms => new Promise(resolve => setTimeout(resolve,ms));
  const wait = async (predicate, description, timeout=7000) => {
    const deadline = performance.now()+timeout;
    while (!predicate()) { if(performance.now()>deadline) throw new Error(`Timeout: ${description}; ${JSON.stringify(lab.state)}`); await sleep(70); }
  };
  const report = (name,passed,detail={}) => invoke(probe,'result',name,passed,detail);
  const check = async (name,expected) => {
    const result=await invoke(probe,'check',name,expected,lab.ui());
    if(!result.ok) throw new Error(`Native assertion failed: ${name}`);
  };
  const click = async id => {
    const element=document.getElementById(id), r=element.getBoundingClientRect(), before=lab.interactions.length;
    await invoke(probe,'click',r.x+r.width/2,r.y+r.height/2);
    await wait(()=>lab.interactions.length>before && lab.interactions.at(-1).done,`HTML click ${id}`,3000);
    const event=lab.interactions.at(-1);
    if(event.id!==id || !event.isTrusted || event.error || event.result?.ok===false) throw new Error(`Click ${id}: ${JSON.stringify(event)}`);
    return event;
  };
  const capture = async name => { await adapter.layout(); await sleep(450); const r=await invoke(probe,'capture',name,lab.ui()); if(!r.ok)throw new Error('capture failed'); };
  const seek = async value => { document.getElementById('seek').value=value; await click('seekButton'); await wait(()=>Math.abs(lab.state.position-value)<.25,'seek '+value); };
  const nativeReady = async () => { await wait(()=>lab.state.loaded && lab.state.decodedWidth>0,'native loaded/decoded'); };
  const progress=document.getElementById('testStatus');
  try {
    const prior=JSON.parse(sessionStorage.getItem('priorDocument')||'null');
    if(prior) {
      sessionStorage.removeItem('priorDocument');
      const rejected=await invoke(remote,'act',prior.document,prior.session,'play',0);
      await report('stale document rejected after real page reload',!rejected.ok && prior.document!==lab.documentId,{oldDocument:prior.document,newDocument:lab.documentId,rejected});
      await click('loadH264'); await nativeReady();
      await check('reopen after document reload',{loaded:true,decodedWidthMin:1,paused:true});
      await capture('document-reopen-before-native'); await invoke(probe,'webOpacity',0); await sleep(650); await capture('document-reopen-native-only'); await invoke(probe,'webOpacity',1); await sleep(650); await capture('document-reopen');
      await click('close'); await check('final close releases session',{session:'',surfaceVisible:false});
      await report('native pointer clicks reached HTML handlers',lab.interactions.every(e=>e.isTrusted),{interactions:lab.interactions});
      progress.textContent='Self-test complete. Read native outcomes and pixel verification.';
      await invoke(probe,'finish'); return;
    }
    progress.textContent='Running actual HTML <video> baselines…';
    const video=document.getElementById('baseline');
    for(const fixture of ['h264','ffv1']) {
      const events=[], start=performance.now(); let playError='';
      const types=['loadedmetadata','loadeddata','playing','timeupdate','ended','error'];
      const listeners=types.map(type=>{const handler=()=>events.push({type,time:video.currentTime,error:video.error?.code});video.addEventListener(type,handler);return [type,handler]});
      video.src=lab.boot.baselineSources[fixture]; video.load();
      video.play().catch(e=>{playError=String(e)});
      while(performance.now()-start<4000 && (video.currentTime<.35 || video.videoWidth===0) && !video.error && !playError) await sleep(80);
      const quality=video.getVideoPlaybackQuality?.();
      const detail={fixture,source:'fixed local fixture',canPlayMp4:video.canPlayType('video/mp4; codecs="avc1.64001f, mp4a.40.2"'),canPlayMkv:video.canPlayType('video/x-matroska'),currentTime:video.currentTime,videoWidth:video.videoWidth,videoHeight:video.videoHeight,errorCode:video.error?.code||null,errorMessage:video.error?.message||null,playError,events,elapsedMs:performance.now()-start,totalVideoFrames:quality?.totalVideoFrames||0,droppedVideoFrames:quality?.droppedVideoFrames||0,played:video.currentTime>.2&&video.videoWidth>0};
      await report('HTML video baseline measured: '+fixture,true,detail);
      video.pause(); video.removeAttribute('src');video.load();listeners.forEach(([t,h])=>video.removeEventListener(t,h));
    }
    progress.textContent='Testing HTML controls → WebChannel → unchanged MpvItem…';
    await click('loadH264'); await nativeReady();
    await check('H264 loaded paused',{fixture:'h264',loaded:true,paused:true,decodedWidthMin:1,durationMin:4});
    await invoke(probe,'diagnostics');
    await click('play'); await wait(()=>!lab.state.paused && lab.state.position>.6,'play advances');
    await check('HTML play advances native clock',{paused:false,positionMin:.5});
    await click('pause'); await wait(()=>lab.state.paused,'pause');
    const pausePosition=lab.state.position; await sleep(450);
    await report('paused native clock stays still',Math.abs(lab.state.position-pausePosition)<.12,{before:pausePosition,after:lab.state.position});
    await seek(1); await capture('h264-before-native'); await invoke(probe,'webOpacity',0); await sleep(650); await capture('native-only'); await invoke(probe,'webOpacity',1); await sleep(650); await capture('decoded-frame-a'); await invoke(probe,'diagnostics');
    await seek(3); await capture('decoded-frame-b'); await invoke(probe,'diagnostics');
    await check('seek maps to cached native position',{paused:true,positionMin:2.75,positionMax:3.25});
    document.getElementById('volume').focus(); document.getElementById('volume').value=37; await click('volumeButton'); await wait(()=>lab.state.volume===37,'volume');
    await check('volume maps to native cached property',{volume:37});
    await click('menuButton'); await capture('paused-menu');
    await click('captionButton'); await capture('paused-caption-off');
    await click('captionButton'); await capture('paused-caption-on');
    await report('HTML menu and caption toggles while paused',!document.getElementById('menu').hidden&&!document.getElementById('caption').hidden&&lab.state.paused,lab.ui());
    await invoke(probe,'resize',1100,820); await sleep(550); await adapter.layout(); await capture('resized');
    await check('resize geometry width',{widthMin:1000});
    scrollTo(0,65); await sleep(400); await adapter.layout(); await capture('scrolled');
    await report('scroll adjusts native geometry',Math.abs(lab.state.y-document.getElementById('aperture').getBoundingClientRect().y)<2,lab.ui());
    scrollTo(0,0); await invoke(probe,'zoom',1.25); await sleep(700); await adapter.layout(); await capture('zoomed');
    await report('zoom maps CSS rect to native coordinates',Math.abs(lab.state.width-document.getElementById('aperture').getBoundingClientRect().width*1.25)<2,lab.ui());
    await invoke(probe,'zoom',1); await sleep(550); await adapter.layout();
    await click('fullscreen'); await sleep(600); await adapter.layout(); await capture('fullscreen');
    await report('controlled fullscreen geometry',document.getElementById('aperture').classList.contains('full')&&Math.abs(lab.state.width-innerWidth)<2,lab.ui());
    await click('fullscreen'); await sleep(550); await adapter.layout();
    const oldSession=lab.session;
    await click('loadFfv1'); await nativeReady();
    const stale=await invoke(remote,'act',lab.documentId,oldSession,'play',0);
    await report('stale source session command rejected',!stale.ok,{stale});
    await check('FFV1 replacement loaded',{fixture:'ffv1',loaded:true,paused:true,decodedWidthMin:1});
    await report('source switch displayed volume matches native',Number(document.getElementById('volume').value)===lab.state.volume,{displayed:Number(document.getElementById('volume').value),native:lab.state.volume});
    await invoke(probe,'diagnostics');
    await click('play'); await wait(()=>lab.state.position>.6&&!lab.state.paused,'FFV1 advance');
    await check('FFV1 native playback advances',{fixture:'ffv1',positionMin:.5,paused:false});
    await click('pause'); await wait(()=>lab.state.paused,'FFV1 pause'); await seek(2); await capture('ffv1-before-native'); await invoke(probe,'webOpacity',0); await sleep(650); await capture('ffv1-native-only'); await invoke(probe,'webOpacity',1); await sleep(650); await capture('ffv1-decoded');
    await invoke(probe,'diagnostics');
    // Source replacement invalidates queued old commands immediately, including an immediate close/reopen.
    await click('loadH264'); await click('close'); await click('loadFfv1'); await nativeReady();
    await check('interrupted load close reopen',{fixture:'ffv1',loaded:true,paused:true});
    await seek(Math.max(0,lab.state.duration-.55)); await click('play'); await wait(()=>lab.state.ended,'end event',4500);
    await check('native end event reaches HTML',{ended:true});
    await click('loadMissing'); await wait(()=>!!lab.state.error,'missing error',4500);
    await check('native missing-file error reaches HTML',{fixture:'missing',loaded:false});
    await report('missing error is surfaced without fabricated codec diagnosis',lab.state.error.startsWith('MpvQt end-file:'),{state:lab.state});
    await click('loadH264'); await nativeReady();
    const forbidden=await invoke(remote,'loadFixture',lab.documentId,lab.session,'file:///etc/passwd');
    await report('arbitrary source URL rejected',!forbidden.ok,{forbidden});
    const unknown=await invoke(remote,'act',lab.documentId,lab.session,'raw-mpv-command',0);
    await report('unknown native command rejected',!unknown.ok,{unknown});
    await check('reopen after error',{loaded:true,fixture:'h264'});
    await report('native pointer clicks reached HTML handlers',lab.interactions.every(e=>e.isTrusted),{interactions:lab.interactions});
    sessionStorage.setItem('priorDocument',JSON.stringify({document:lab.documentId,session:lab.session}));
    progress.textContent='Reloading the actual WebEngine document to test stale document rejection…';
    const r=document.getElementById('reload').getBoundingClientRect(); await invoke(probe,'click',r.x+r.width/2,r.y+r.height/2);
  } catch(error) {
    console.error(error.stack||String(error));
    await report('self-test exception',false,{error:String(error),stack:error.stack,state:lab.state,ui:lab.ui()});
    await invoke(probe,'finish');
  }
})();
