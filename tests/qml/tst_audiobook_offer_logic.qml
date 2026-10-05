import QtQuick
import QtTest
import "../../qml/AudiobookOfferLogic.js" as L
TestCase {
    name: "AudiobookOfferLogic"
    function test_states_data() {
        var pick = {pick:{format:"M4B",language:"English",size:"412 MB"},confident:true,candidates:[{}]}
        return [
            {tag:"checking",input:{lookup:{loading:true}},state:"checking",label:"Looking for an audiobook…"},
            {tag:"none",input:{lookup:{rows:[]}},state:"none",label:"No audiobook found"},
            {tag:"unknown",input:{lookup:{error:true}},state:"unknown",label:""},
            {tag:"available",input:{lookup:{rows:[{}]},pick:pick},state:"available",label:"Get audiobook"},
            {tag:"choose",input:{lookup:{rows:[{},{},{},{}]},pick:{confident:false,candidates:[{},{},{},{}]}},state:"choose",label:"Choose audiobook"},
            {tag:"getting",input:{status:{state:"downloading",received:37,total:100}},state:"getting",label:"Getting audiobook · 37%"},
            {tag:"failed",input:{failed:true},state:"failed",label:"Couldn't get the audiobook · Try again"},
            {tag:"ready",input:{downloaded:true,entry:{format:"M4B",sizeLabel:"412 MB"}},state:"ready",label:"Listen"},
            {tag:"ready-precedence",input:{downloaded:true,failed:true,lookup:{loading:true},status:{state:"downloading"}},state:"ready",label:"Listen"},
            {tag:"getting-precedence",input:{lookup:{error:true},status:{state:"resolving"}},state:"getting",label:"Getting audiobook · 0%"},
            {tag:"failed-precedence",input:{failed:true,lookup:{rows:[{}]},pick:pick},state:"failed",label:"Couldn't get the audiobook · Try again"},
            {tag:"builtin",input:{builtIn:true},state:"ready",label:"Listen"}
        ]
    }
    function test_states(data) {
        var out=L.offerState(data.input)
        compare(out.offerState,data.state); compare(out.label,data.label)
        if(data.tag==="available") compare(out.meta,"M4B · English · 412 MB")
        if(data.tag==="choose") compare(out.meta,"4 possible matches · none certain")
        if(data.tag==="ready") compare(out.meta,"M4B · 412 MB")
        if(data.tag==="builtin") compare(out.meta,"Built-in narration")
        if(data.tag==="getting") compare(out.progress,0.37)
    }
}
