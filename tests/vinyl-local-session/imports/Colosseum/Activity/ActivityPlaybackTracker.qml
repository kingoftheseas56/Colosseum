import QtQuick
// Call-order model only. Native tracker tests prove wall/media deltas and persistence.
QtObject {
    property var sink:null
    property var calls:[]
    function add(method,args) {calls=calls.concat([{method:method,args:args}]);}
    function begin(identity,id) {add("begin",[identity,id]);}
    function sample(position,duration,rate,consuming) {add("sample",[position,duration,rate,consuming]);}
    function discontinuity(position,duration,rate) {add("discontinuity",[position,duration,rate]);}
    function naturalEof(position,duration) {add("eof",[position,duration]);}
    function endSession(position,duration) {add("end",[position,duration]);}
    function playbackStateChanged(playing,position,duration) {add("state",[playing,position,duration]);}
}
