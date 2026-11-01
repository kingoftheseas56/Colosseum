(function suite(){
"use strict";
const fs=require("fs"),path=require("path");
exports.run=async(env,cmd)=>{
const loadModule=n=>{const p=require.resolve("./"+n);delete require.cache[p];return require(p);};
const results={startedUtc:new Date().toISOString(),codeFrozen:true};
results.scenarios=await loadModule("scenario-tests.cjs").run(env,{});
results.regressions=await loadModule("interaction-probes.cjs").run(env,{variant:"after"});
results.keyboard=await loadModule("keyboard-decision.cjs").run(env,{});
results.matrix=await loadModule("qa-core.cjs").run(env,{variant:"after",cases:[{name:"portrait",kind:"portrait",bottom:true},{name:"landscape",kind:"landscape",bottom:true},{name:"desktop",kind:"desktop",bottom:true},{name:"small-stress",kind:"small",stress:true,bottom:true},{name:"short-stress",kind:"short",stress:true,bottom:true},{name:"tablet-portrait",kind:"tablet",bottom:true},{name:"tablet-landscape",kind:"tabletLandscape",bottom:true}]});
results.completedUtc=new Date().toISOString();fs.writeFileSync(path.join(env.out,"after","FINAL-SUITE.json"),JSON.stringify(results,null,2)+"\n");
return {scenarios:results.scenarios,regressions:results.regressions,keyboard:results.keyboard,matrix:results.matrix};
};
})();
