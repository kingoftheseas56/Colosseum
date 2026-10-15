// Minimal Feria-owned registry. The trusted provider argument is supplied by the
// native host's wrapper, never selected from page metadata. Connectors return data.
var registrySource = [
    "function registry(provider, connector, generic) {",
    " if(provider!=='ytmusic' || !connector) return {outcome:'not-applicable'}",
    " function safe(s) {return s && !s.ad && Number.isFinite(s.position) && s.position>=0",
    "   && Number.isFinite(s.duration) && s.duration>=30 && s.position<=s.duration",
    "   && (!s.identityProvider || s.identityProvider===provider)}",
    " var result;try {result=connector(provider)} catch(e) {return {outcome:'error',reason:'connector-exception'}}",
    " if(!result) return {outcome:'hold',reason:'connector-uncertain'}",
    " if(result.outcome==='not-applicable' || result.outcome==='hold') return result",
    " if(result.outcome==='observation') {",
    "   if(!safe(result.sample)) return {outcome:'hold',reason:'unsafe-observation'}",
    "   return result",
    " }",
    " if(result.outcome==='error') {",
    "   // A caught exception alone never supplies this proof. An explicit same",
    "   // player/snapshot proof can permit a generic sample, still resume-gated.",
    "   var p=result.fallbackProof",
    "   if(p && p.provider===provider && p.media && generic) {",
    "     var s=generic()",
    "     if(safe(s) && p.media===window.__feriaProbeMedia && p.href===s.href",
    "       && p.title===s.title && p.position===s.position && p.duration===s.duration)",
    "       return {outcome:'observation',sample:s,fallback:true}",
    "   }",
    "   return {outcome:'error',reason:result.reason || 'connector-error'}",
    " }",
    " return {outcome:'hold',reason:'unknown-connector-outcome'}",
    "}"
].join("\n")
function script(connector) {
    return '(function(provider,generic){return (' + registrySource + ')(provider,'
        + (connector || 'null') + ',generic)})'
}
