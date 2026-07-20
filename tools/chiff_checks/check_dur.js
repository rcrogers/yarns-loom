const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
function noiseEnd(dur){ // ms at which chiff noise last exceeds 1% peak
  const p={attack:1200,decay:1,peak:0.7,sustain:0.6,release:2000,gate:1500,
    amount:96,warp:1.0,chiffDur:dur,dutyMode:'block',slewMethod:'geometric',curve:'expo',k:4.0,dutySeed:0,seed:0xCAFEBABE};
  const r=mod.render(p); let last=0;
  for(let i=1;i<r.totalN;i++) if(Math.abs(r.out[i]-r.out[i-1])>0.01*PEAK) last=i;
  return last/FS*1000;
}
console.log('attack=1200ms. chiff noise should end ~at the chiff-duration setting:');
for(const d of [200,600,1200,2400]) console.log(`  chiffDur=${d}ms -> noise ends ~${noiseEnd(d).toFixed(0)}ms`);
