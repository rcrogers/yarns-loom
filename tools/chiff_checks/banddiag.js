// Diagnose banding near attack peak at low amount: per-hop AC + run state.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
let code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
// capture per-run state
code=code.replace('runW = w;',
  'runW = w; if(global._st) global._st.push({i, w, up:runUp, dn:runDown, c:center, base, alpha});');
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
for(const amount of [32,96]){
  global._st=[];
  const p={attack:130,decay:256,peak:0.75,sustain:0.55,release:401,gate:575,
    amount,chiffDur:601,seed:0xCAFEBABE};
  const r=mod.render(p);
  // per-hop AC 60-200ms
  const hops=[];
  for(let h=60*45;h+64<=200*45;h+=64){let s=0;
    for(let i=h;i<h+64;i++)s+=Math.abs(r.out[i]-r.out[i-1]);
    hops.push(s/64/PEAK*1e3);}
  const mean=hops.reduce((a,b)=>a+b)/hops.length;
  const dark=hops.filter(x=>x<0.3*mean).length;
  console.log(`amt=${amount}: hops 60-200ms mean AC ${mean.toFixed(2)}, dark(<30%) ${dark}/${hops.length}, min ${(Math.min(...hops)/mean*100).toFixed(0)}%`);
  const minIdx=hops.indexOf(Math.min(...hops));
  const t=60*45+minIdx*64;
  const st=global._st.filter(s=>Math.abs(s.i-t)<200);
  for(const s of st.slice(0,3)) console.log(
    `  run@${(s.i/45).toFixed(1)}ms w=${s.w.toFixed(3)} up=${(s.up/PEAK*100).toFixed(1)}% dn=${(s.dn/PEAK*100).toFixed(1)}% center=${(s.c/PEAK*100).toFixed(1)}% base=${(s.base/PEAK*100).toFixed(1)}% alpha=${s.alpha.toFixed(4)}`);
}
