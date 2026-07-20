// FINE peak-dip gate: 8ms-window hiRMS through the attack peak. The dip
// metric = deepest window in [peak-40ms, peak+40ms] relative to the median
// of the preceding 40ms. This is the metric that must catch what coarse
// windows averaged away. Args: <sim.html> [seedHex]
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const seed=process.argv[3]?parseInt(process.argv[3],16)>>>0:0xCAFEBABE;
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
let fails=0;
for(const amount of [16,42,96,127]){
  const p={attack:130,decay:256,peak:0.75,sustain:0.55,release:401,gate:575,
    amount,chiffDur:601,seed};
  const r=mod.render(p);
  const win=[];
  for(let t=50;t+8<=260;t+=8){let s=0,n=0;
    for(let i=t*45;i<(t+8)*45;i++){const st=r.out[i]-r.out[i-1];s+=st*st;n++;}
    win.push({t,v:Math.sqrt(s/n)/PEAK*1e3});}
  const peakT=130;
  const pre=win.filter(w=>w.t>=peakT-48&&w.t<peakT-8).map(w=>w.v).sort((a,b)=>a-b);
  const preMed=pre[Math.floor(pre.length/2)];
  const zone=win.filter(w=>w.t>=peakT-40&&w.t<=peakT+40);
  const deepest=Math.min(...zone.map(w=>w.v));
  const ratio=deepest/preMed*100;
  const decayZone=win.filter(w=>w.t>peakT+40&&w.t<=peakT+120);
  const ok=ratio>60;  // no window in the peak zone below 60% of pre-peak level
  if(!ok)fails++;
  console.log(`amt=${String(amount).padStart(3)}: pre-peak med ${preMed.toFixed(1)}, deepest@peak ${deepest.toFixed(1)} (${ratio.toFixed(0)}%) ${ok?'OK':'DIP'}`
    +`  profile: ${win.filter(w=>w.t%24<8).map(w=>w.v.toFixed(0)).join(' ')}`);
}
process.exit(fails?1:0);
