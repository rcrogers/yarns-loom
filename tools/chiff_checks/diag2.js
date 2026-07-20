const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
const p={attack:1200,decay:1,peak:0.7,sustain:0.6,release:400,gate:2000,
  amount:96,chiffDur:600,seed:0xCAFEBABE};
const r=mod.render(p); const d=mod.render({...p,amount:0}).out;
console.log(' t(ms) | DCbias(out-dialed) | ACrms(out-localmean) | dialed%');
for(let t=0;t<=650;t+=50){ let a=t*45,b=(t+50)*45, s=0,n=0;
  for(let i=a;i<b&&i<r.totalN;i++){s+=r.out[i]-d[i];n++;}
  const bias=s/n;
  let ac=0,na=0; const w=32;
  for(let i=a+w;i<b-w&&i<r.totalN;i++){let m=0;for(let j=-w;j<=w;j++)m+=r.out[i+j];m/=(2*w+1);ac+=(r.out[i]-m)**2;na++;}
  console.log(`  ${String(t).padStart(4)} | ${(bias/PEAK*100).toFixed(2).padStart(7)}% | ${(Math.sqrt(ac/na)/PEAK*100).toFixed(2).padStart(6)}% | ${(d[a]/PEAK*100).toFixed(0)}`);
}
