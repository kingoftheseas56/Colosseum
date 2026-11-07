// Runtime automation of the installed, unmodified Main and native owners.
// Not a replacement source resolver, mock player, or shipping QML file.
import QtQuick
import "file:///opt/colosseum/qml" as Installed
Installed.Main {
    id: app
    width: 1280; height: 720
    property int proofStage: 0
    property int ticks: 0
    property int phaseTicks: 0
    property var proofSheet: null
    property string repoId: ""
    property string roster: ""
    property string stremioRows: ""
    property real firstPosition: -1
    property string fault: ""
    property string concurrencyRepoId: ""
    property int enableSlot: 0
    readonly property string concurrencyRoot: Qt.application.arguments[4]
    readonly property string fixtureRoot: Qt.application.arguments[2]
    readonly property string evidenceDirectory: Qt.application.arguments[3]
    function record(stage, details) {
        console.info("LINUX_NUVIO_PROOF=" + JSON.stringify({stage: stage, tick: ticks, details: details || {}}))
    }
    function fail(message) { cycle.stop(); record("FAIL", {message:message}); Qt.exit(1) }
    function advance(stage) { proofStage=stage; phaseTicks=0 }
    function findItem(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        const children=item.children || []
        for(let i=0;i<children.length;i++) { const result=findItem(children[i],name); if(result)return result }
        return null
    }
    function capture(name) {
        const accepted=contentItem.grabToImage(function(result) {
            if(!result.saveToFile(evidenceDirectory+"/"+name+".png")) fail("screenshot save failed: "+name)
        })
        if(!accepted) fail("screenshot request failed: "+name)
    }
    function stremioProofRows() {
        return proofSheet.rows.filter(row => row.addonId === "org.colosseum.proof.publicdomain")
    }
    function queryAll() { proofSheet.show("movie","tt0063350","Night of the Living Dead (1968)",{tmdbId:10331,title:"Night of the Living Dead (1968)"}) }
    function queryNuvio() { proofSheet.nuvioRows=[]; proofSheet.startNuvio("movie","tt0063350",{tmdbId:10331}) }
    function checkIntact() {
        if(JSON.stringify(Extensions.installed()) !== roster) { fail("Stremio registry changed"); return false }
        if(JSON.stringify(stremioProofRows()) !== stremioRows) { fail("Stremio source rows changed"); return false }
        return true
    }
    Timer { id:cycle; interval:200; running:true; repeat:true; onTriggered: {
        app.ticks++; app.phaseTicks++
        if(app.ticks>1800) { app.fail("overall watchdog"); return }
        if(app.phaseTicks>500) { app.fail("stage watchdog "+app.proofStage); return }
        if(app.proofStage===0) {
            const splash=app.findItem(app.contentItem,"bootSplash")
            if(!splash || splash.visible || AccountController.busy)return
            const welcome=app.findItem(app.contentItem,"accountWelcome")
            if(!welcome || !welcome.visible)return
            app.capture("fresh-profile-welcome")
            app.record("welcome",{visible:welcome.visible})
            app.advance(-1);return
        }
        if(app.proofStage===-1) {
            if(app.phaseTicks<10)return
            AccountController.continueWithoutAccount()
            app.showNormal();app.width=1280;app.height=720
            app.record("fresh_profile",{repositories:NuvioRepos.repositories.length})
            if(NuvioRepos.repositories.length!==0){app.fail("profile was not fresh");return}
            NuvioRepos.addRepository(app.fixtureRoot+"/manifest.json")
            app.advance(1);return
        }
        if(app.proofStage===1) {
            if(NuvioRepos.busy)return
            if(NuvioRepos.error.length){app.fail(NuvioRepos.error);return}
            if(NuvioRepos.repositories.length!==1){app.fail("repo not installed");return}
            app.repoId=NuvioRepos.repositories[0].id
            NuvioRepos.setPluginEnabled(app.repoId,"public-domain-archive",true)
            Extensions.install(app.fixtureRoot+"/stremio/manifest.json")
            app.advance(2);return
        }
        if(app.proofStage===2) {
            if(NuvioRepos.busy || !Extensions.isInstalled("org.colosseum.proof.publicdomain"))return
            if(!NuvioRepos.repositories[0].plugins.some(p=>p.id==="public-domain-archive"&&p.enabled)){app.fail("plugin not enabled");return}
            app.record("repository_enabled",{address:app.fixtureRoot+"/manifest.json",plugins:NuvioRepos.repositories[0].plugins})
            app.roster=JSON.stringify(Extensions.installed())
            app.openWorld("Theatre")
            app.openTheatreSeries({id:"tt0063350",type:"movie",title:"Night of the Living Dead",name:"Night of the Living Dead",tmdbId:10331})
            app.advance(3);return
        }
        if(app.proofStage===3) {
            app.proofSheet=app.findItem(app.contentItem,"sourcesSheet")
            if(!app.proofSheet)return
            app.queryAll();app.advance(4);return
        }
        if(app.proofStage===4) {
            if(app.proofSheet.loading || app.proofSheet.nuvioPending)return
            if(!app.proofSheet.nuvioRows.length || !app.stremioProofRows().length){app.fail("missing live Nuvio/Stremio fixture rows");return}
            const options=app.proofSheet.providerOptions
            const nuvio=options.filter(option=>option.key==="nuvio")
            if(nuvio.length!==1){app.fail("Nuvio provider entry count: "+JSON.stringify(options));return}
            app.stremioRows=JSON.stringify(app.stremioProofRows())
            app.record("sources",{nuvioRows:app.proofSheet.nuvioRows,stremioFixtureRows:app.stremioProofRows(),providerOptions:options,askedNames:app.proofSheet.askedNames})
            app.proofSheet.extMenuOpen=true
            app.capture("sources-providers")
            app.advance(5);return
        }
        if(app.proofStage===5) {
            if(app.phaseTicks<10)return
            app.proofSheet.extMenuOpen=false
            const index=app.proofSheet.visibleRows.findIndex(row=>row.nuvio===true)
            if(index<0){app.fail("no Nuvio play row");return}
            app.proofSheet.activateSource(index);app.firstPosition=-1;app.advance(6);return
        }
        if(app.proofStage===6 || app.proofStage===16) {
            const player=app.playerLayerRef.item
            if(!player)return
            if(player.errored){app.fail("player error");return}
            if(!player.playerReady || !player.playbackStarted || player.decodedWidth<=0 || player.decodedHeight<=0)return
            const position=Number(player.playbackPosition||0)
            if(app.firstPosition<0){app.firstPosition=position;return}
            if(position<=app.firstPosition+1)return
            app.record(app.proofStage===6?"nuvio_playback":"stremio_playback",{firstPosition:app.firstPosition,position:position,decodedWidth:player.decodedWidth,decodedHeight:player.decodedHeight})
            app.capture(app.proofStage===6?"nuvio-playback":"stremio-playback")
            app.advance(app.proofStage===6?7:17);return
        }
        if(app.proofStage===7) {
            if(app.phaseTicks<10)return
            app.closePlayerSession(); app.queryAll();app.advance(8);return
        }
        if(app.proofStage===8) {
            if(app.proofSheet.loading||app.proofSheet.nuvioPending)return
            if(!app.proofSheet.nuvioRows.length||!app.checkIntact())return
            app.fault="crash";app.advance(9);return
        }
        if(app.proofStage===9) {
            // External Python driver sends SIGKILL/SIGSTOP only to the child
            // helper of this specific owned application PID.
            app.record("fault_armed",{kind:app.fault})
            app.queryNuvio();app.advance(10);return
        }
        if(app.proofStage===10) {
            if(app.proofSheet.nuvioPending)return
            if(app.proofSheet.nuvioRows.length){app.fail("fault returned success unexpectedly");return}
            if(!app.checkIntact())return
            app.record("fault_complete",{kind:app.fault,elapsedMs:app.phaseTicks*200,responsiveTicks:app.phaseTicks})
            app.capture(app.fault+"-survived")
            app.record("recovery_armed",{kind:app.fault})
            app.queryNuvio();app.advance(11);return
        }
        if(app.proofStage===11) {
            if(app.proofSheet.nuvioPending)return
            if(!app.proofSheet.nuvioRows.length){app.fail("next request did not recover");return}
            if(!app.checkIntact())return
            app.record("recovery",{kind:app.fault,nuvioRows:app.proofSheet.nuvioRows.length,stremioRows:app.stremioProofRows().length})
            app.capture(app.fault+"-recovered")
            app.advance(12);return
        }
        if(app.proofStage===12) {
            if(app.phaseTicks<10)return
            if(app.fault==="crash"){app.fault="timeout";app.advance(9);return}
            app.queryAll();app.advance(15);return
        }
        if(app.proofStage===15) {
            if(app.proofSheet.loading||app.proofSheet.nuvioPending)return
            if(!app.proofSheet.nuvioRows.length||!app.checkIntact())return
            app.record("final_requery",{nuvioRows:app.proofSheet.nuvioRows.length,stremioRows:app.stremioProofRows().length})
            const index=app.proofSheet.visibleRows.findIndex(row=>row.addonId==="org.colosseum.proof.publicdomain"&&!row.nuvio)
            if(index<0){app.fail("Stremio play row missing");return}
            app.proofSheet.activateSource(index);app.firstPosition=-1;app.advance(16);return
        }
        if(app.proofStage===17 && app.phaseTicks>=10) {
            app.closePlayerSession()
            NuvioRepos.setPluginEnabled(app.repoId,"public-domain-archive",false)
            NuvioRepos.addRepository(app.concurrencyRoot+"/manifest.json")
            app.advance(18);return
        }
        if(app.proofStage===18) {
            if(NuvioRepos.busy)return
            if(NuvioRepos.error.length){app.fail(NuvioRepos.error);return}
            const repo=NuvioRepos.repositories.find(repo=>repo.id!==app.repoId)
            if(!repo || repo.plugins.length!==4){app.fail("concurrency repo missing");return}
            app.concurrencyRepoId=repo.id;app.enableSlot=1
            NuvioRepos.setPluginEnabled(repo.id,"slot-1",true)
            app.advance(19);return
        }
        if(app.proofStage===19) {
            if(NuvioRepos.busy)return
            if(NuvioRepos.error.length){app.fail(NuvioRepos.error);return}
            if(app.enableSlot<4) {
                app.enableSlot++
                NuvioRepos.setPluginEnabled(app.concurrencyRepoId,"slot-"+app.enableSlot,true)
                return
            }
            app.record("concurrency_armed",{repository:app.concurrencyRoot,expectedWorkers:4})
            app.queryAll();app.advance(20);return
        }
        if(app.proofStage===20) {
            if(app.proofSheet.loading||app.proofSheet.nuvioPending)return
            if(app.proofSheet.nuvioRows.length!==4 || !app.checkIntact()){app.fail("four-worker results missing");return}
            app.record("concurrency_complete",{rows:app.proofSheet.nuvioRows.length,providerOptions:app.proofSheet.providerOptions})
            app.capture("four-workers-complete")
            app.advance(21);return
        }
        if(app.proofStage===21 && app.phaseTicks>=10) {
            app.record("PASS",{ticks:app.ticks,registryUnchanged:JSON.stringify(Extensions.installed())===app.roster})
            cycle.stop();Qt.exit(0)
        }
    }}
}
