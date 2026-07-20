const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
let code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
// log alpha & up-count to see balance + freeze
code=code.replace('value += (tgt - value) * alphaOf(shChiff);',
  'if(_L){ _L.push([alphaOf(shChiff), tgt-dialed, value-dialed]); } value += (tgt - value) * alphaOf(shChiff);');
global._L=null;
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000,PEAK=(1<<30)-(1<<15);
global._L=[];
const p={attack:1200,decay:1,peak:0.7,sustain:0.6,release:400,gate:2000,amount:96,chiffDur:600,seed:0xCAFEBABE};
mod.render(p);
// summarize dart target balance & alpha near t=100ms and t=500ms
function summ(t){ const i0=t*45, L=global._L.slice(i0,i0+2000);
  let up=0,dn=0,su=0,sd=0; for(const [al,dt,vd] of L){ if(dt>1) {up++;su+=dt;} else if(dt<-1){dn++;sd+=dt;} }
  console.log(`t=${t}ms: alpha=${L[0][0].toExponential(2)} | up darts=${up}(avg ${(su/up/PEAK*100).toFixed(1)}%) dn darts=${dn}(avg ${(sd/dn/PEAK*100).toFixed(1)}%) | value-dialed=${(L[0][2]/PEAK*100).toFixed(2)}%`);
}
summ(100); summ(300); summ(500);
