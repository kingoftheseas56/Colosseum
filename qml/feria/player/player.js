'use strict';
new QWebChannel(qt.webChannelTransport, async channel => {
    const player=channel.objects.player, el=id=>document.getElementById(id);
    const invoke=(method,...args)=>new Promise(resolve=>player[method](...args,resolve));
    let state=await invoke('bootstrap'), seeking=false;
    const format=value=>{value=Math.max(0,Math.floor(Number(value)||0));return `${Math.floor(value/60)}:${String(value%60).padStart(2,'0')}`};
    const command=(action,value=0)=>invoke('act',state.document,state.session,action,value);
    function render(next) {
        state=next;
        el('toggle').textContent=next.ended?'Replay':next.paused?'Play':'Pause';
        el('time').textContent=`${format(next.position)} / ${format(next.duration)}`;
        el('message').textContent=next.error || (next.ready?'':'Opening video…');
        el('seek').max=Number(next.duration)||0;
        if(!seeking)el('seek').value=Number(next.position)||0;
        if(document.activeElement!==el('volume'))el('volume').value=next.volume;
        if(document.activeElement!==el('rate'))el('rate').value=String(next.rate);
        for(const id of ['toggle','seek','volume','rate'])el(id).disabled=!next.ready||next.suspended||!!next.error;
    }
    player.state.connect(render); render(state);
    el('toggle').onclick=async()=>{if(state.ended)await command('seek',0);await command(state.paused||state.ended?'play':'pause')};
    el('seek').oninput=()=>{seeking=true};
    el('seek').onchange=async()=>{await command('seek',Number(el('seek').value));seeking=false};
    el('volume').oninput=()=>command('volume',Number(el('volume').value));
    el('rate').onchange=()=>command('rate',Number(el('rate').value));
    el('close').onclick=()=>command('close');
    el('fullscreen').onclick=()=>command('fullscreen');
    document.addEventListener('keydown',event=>{
        if(event.target.matches('input,select,button'))return;
        if(event.code==='Space'){event.preventDefault();el('toggle').click()}
        else if(event.code==='ArrowRight'){event.preventDefault();command('seek',Math.min(state.duration,state.position+5))}
        else if(event.code==='ArrowLeft'){event.preventDefault();command('seek',Math.max(0,state.position-5))}
    });
    document.body.dataset.playerReady='true';
});
