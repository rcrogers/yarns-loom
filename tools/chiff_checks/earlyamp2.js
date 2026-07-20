// Early-chiff EXCURSION (RMS of out-dialed, % of PEAK) vs duration.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);

function measure(durMs, amount){
  const base={attack:1200,decay:1,peak:0.7,sustain:0.6,release:2000,gate:9000,
    seed:0xCAFEBABE,chiffDur:durMs};
  const r=mod.render(Object.assign({},base,{amount}));
  const d=mod.render(Object.assign({},base,{amount:0}));
  const win=(aMs,bMs)=>{let s=0,n=0;
    for(let i=aMs*45;i<bMs*45;i++){const e=r.out[i]-d.out[i];s+=e*e;n++;}
    return Math.sqrt(s/n)/PEAK*100;};
  return [win(0,50),win(50,100),win(100,200),win(200,400)];
}
for(const amount of [32,64,96,127]){
  console.log(`amount=${amount}`);
  for(const d of [1000,8000]){
    const m=measure(d,amount).map(x=>x.toFixed(2)).join('  ');
    console.log(`  dur=${String(d).padStart(4)}ms  excursion% [0-50|50-100|100-200|200-400ms]: ${m}`);
  }
}
