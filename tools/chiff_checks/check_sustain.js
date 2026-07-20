const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
// short attack/decay -> reach sustain fast; long chiff into sustain; hold then release
const p={attack:100,decay:50,peak:0.7,sustain:0.6,release:200,gate:600,
  amount:96,warp:1.0,chiffDur:800,dutyMode:'block',slewMethod:'geometric',curve:'expo',k:4.0,dutySeed:0,seed:0xCAFEBABE};
const r=mod.render(p); const susLvl=0.6*0.7*100;
console.log(`sustain level = ${susLvl.toFixed(1)}%. chiffDur=800ms, reach sustain ~150ms, release @600ms`);
console.log(' t(ms) | mean%  | noise(x1e3)   [sustain region 150-600ms should have fading motion]');
for(let t=100;t<=700;t+=50){ let s=0,nz=0,n=0,a=t*45,b=(t+50)*45;
  for(let i=Math.max(1,a);i<b&&i<r.totalN;i++){s+=r.out[i];if(i>a)nz+=Math.abs(r.out[i]-r.out[i-1]);n++;}
  const stage = t<150?'atk/dec':t<600?'SUSTAIN':'release';
  console.log(`  ${String(t).padStart(4)} | ${(s/n/PEAK*100).toFixed(1).padStart(5)} |  ${(nz/n/PEAK*1e3).toFixed(2).padStart(5)}   ${stage}`);
}
