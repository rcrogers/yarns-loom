const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
let code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
code=code.replace('let chiffLeft = drop','global._CL0=drop?Math.round(p.chiffDur*45):0; let chiffLeft = drop');
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
const p={attack:1200,decay:1,peak:0.7,sustain:0.6,release:2000,gate:1500,
  amount:96,warp:1.0,chiffDur:200,dutyMode:'block',slewMethod:'geometric',curve:'expo',k:4.0,dutySeed:0,seed:0xCAFEBABE};
const r=mod.render(p);
console.log('chiffDur=200ms -> armed chiffLeft =',global._CL0,'samples =',(global._CL0/45).toFixed(0),'ms');
console.log('noise (mean|step| over 10ms windows):');
for(let t=0;t<400;t+=40){ let s=0,n=0,a=t*45,b=(t+40)*45;
  for(let i=Math.max(1,a);i<b;i++){s+=Math.abs(r.out[i]-r.out[i-1]);n++;}
  console.log(`  ${String(t).padStart(4)}ms: ${(s/n/PEAK*1e3).toFixed(2)}`);}
