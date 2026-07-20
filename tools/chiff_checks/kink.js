// Kink at chiff-window termination: |smoothed out - dialed| in the 150ms
// after the window ends, for window-ends-in-decay and window-ends-in-release.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
function kink(p,endMs,label){
  const r=mod.render(p), d=mod.render(Object.assign({},p,{amount:0}));
  let worst=0;
  for(let w=endMs;w<endMs+150;w+=10){
    let m=0,n=0;
    for(let i=w*45;i<(w+10)*45&&i<r.totalN;i++){m+=r.out[i]-d.out[i];n++;}
    if(n)worst=Math.max(worst,Math.abs(m/n));
  }
  console.log(`  ${label}: worst 10ms-mean gap ${(worst/PEAK*100).toFixed(2)}% of FS`);
}
const base={attack:130,decay:256,peak:0.75,sustain:0.55,release:401,gate:575,
  amount:96,seed:0xCAFEBABE};
kink(Object.assign({},base,{chiffDur:300}),300,'window ends in decay (300ms) ');
kink(Object.assign({},base,{chiffDur:800}),800,'window ends in release (800ms)');
