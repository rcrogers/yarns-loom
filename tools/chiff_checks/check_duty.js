const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
let code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
code=code.replace('let sampleTarget = target;',
  'if(si===3 && chiffActive){ _DBG.push([(stageTotal-samplesLeft)/stageTotal, duty, shiftQ, chiffLeft, S, nominal]); }\n    let sampleTarget = target;');
global._DBG=[];
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
function run(gate,rel,tag){
  _DBG.length=0;
  const p={attack:1200,decay:1,peak:0.7,sustain:0.6,release:rel,gate,
    amount:96,warp:1.0,floor:0,dutyMode:'block',slewMethod:'geometric',curve:'expo',k:4.0,dutySeed:0,seed:0xCAFEBABE};
  mod.render(p);
  const N=_DBG.length; if(!N){console.log(tag,'no chiff in release');return;}
  console.log(`\n=== ${tag}: release @${gate}ms, rel ${rel}ms | chiff samples in release=${N} (${(N/FS*1000).toFixed(0)}ms), stage=${(_DBG.length&&0)||(rel)}ms`);
  console.log('  phi  | P(peak)% | shiftInt | dropToNominal(shift vs nom)');
  for(let f=0;f<=10;f++){ const idx=Math.min(N-1,Math.round(f/10*(N-1)));
    const [phi,duty,shift,cl,S,nom]=_DBG[idx];
    console.log(`  ${phi.toFixed(2)} |  ${((1-duty/65536)*100).toFixed(2).padStart(5)}  |   ${(shift/(1<<27)).toFixed(1).padStart(4)}   | nominal=${(nom/(1<<27)).toFixed(1)} ${shift>=nom-0.05*(1<<27)?'<=reached':''}`);
  }
}
run(6,550,'very early release (chiff remaining >> stage)');
run(900,1500,'late release, long rel (chiff remaining << stage)');
