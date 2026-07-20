// 1s vs 8s chiff duration at the sim's DEFAULT knob positions (early release:
// gate ~60ms into a ~1200ms attack, release ~400ms). Excursion RMS per window.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
const ms=v=>Math.round(Math.exp((v/1000)*Math.log(8000)));  // slider mapping

const defaults={attack:ms(789),decay:ms(0),peak:0.7,sustain:0.6,
  release:ms(667),gate:ms(456),amount:96,seed:0xCAFEBABE};
console.log('defaults: attack',defaults.attack,'ms gate',defaults.gate,
  'ms release',defaults.release,'ms');

for(const dur of [1000,8000]){
  const r=mod.render(Object.assign({},defaults,{chiffDur:dur}));
  const d=mod.render(Object.assign({},defaults,{chiffDur:dur,amount:0}));
  console.log(`  chiffDur=${dur}ms`);
  for(const [a,b] of [[0,20],[20,40],[40,60],[60,120],[120,240],[240,460]]){
    let s=0,n=0;
    for(let i=a*45;i<Math.min(b*45,r.totalN);i++){const e=r.out[i]-d.out[i];s+=e*e;n++;}
    console.log(`    ${String(a).padStart(3)}-${String(b).padEnd(3)}ms: ${(Math.sqrt(s/n)/PEAK*100).toFixed(2)}%`);
  }
}
