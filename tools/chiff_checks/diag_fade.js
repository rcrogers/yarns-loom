const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
const p={attack:1200,decay:1,peak:0.7,sustain:0.6,release:400,gate:2000,
  amount:96,chiffDur:600,dutyMode:'block',slewMethod:'geometric',curve:'expo',k:4,dutySeed:0,seed:0xCAFEBABE};
const r=mod.render(p); const d=mod.render({...p,amount:0}).out;
console.log('chiffDur=600ms. noise = mean|out-dialed| per 25ms window. Should fade smoothly to 0 near 600ms:');
for(let t=0;t<=700;t+=25){ let s=0,n=0,a=t*45,b=(t+25)*45;
  for(let i=a;i<b&&i<r.totalN;i++){s+=Math.abs(r.out[i]-d[i]);n++;}
  const bar='#'.repeat(Math.round(s/n/PEAK*400));
  console.log(`  ${String(t).padStart(4)}ms ${(s/n/PEAK*100).toFixed(2).padStart(5)}% ${bar}`);
}
