// Prototype: when a dart's side has no room (would clamp), flip it to the other
// side. Measure the 8s profile (should be monotone, loud onset) and mean bias.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
let code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
code=code.replace(
  "dartTgt = (r & 0x10000) ? amp : -amp;",
  `dartTgt = (r & 0x10000) ? amp : -amp;
        if (dialed + dartTgt < 0) dartTgt = amp;
        else if (dialed + dartTgt > maxLevel) dartTgt = -amp;`);
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);

function run(durMs){
  const base={attack:1200,decay:1,peak:0.7,sustain:0.6,release:2000,gate:9000,
    seed:0xCAFEBABE,chiffDur:durMs};
  const r=mod.render(Object.assign({},base,{amount:96}));
  const d=mod.render(Object.assign({},base,{amount:0}));
  console.log(`  dur=${String(durMs).padStart(4)}ms   excursionRMS%   meanBias% (out-dialed avg)`);
  for(const [a,b] of [[0,50],[50,100],[100,200],[200,400],[400,800],[800,1200]]){
    let s=0,m=0,n=0;
    for(let i=a*45;i<b*45;i++){const e=r.out[i]-d.out[i];s+=e*e;m+=e;n++;}
    console.log(`    ${String(a).padStart(3)}-${String(b).padEnd(4)}ms:  ${(Math.sqrt(s/n)/PEAK*100).toFixed(2).padStart(6)}        ${(m/n/PEAK*100).toFixed(2).padStart(6)}`);
  }
}
run(1000);
run(8000);
