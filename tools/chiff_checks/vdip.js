// V-shape dip detector: a genuine peak dip is a local minimum BELOW the
// decay trend followed by RECOVERY -- pure burst decay is monotone and must
// pass. For each amount: 8ms hiRMS windows; find min in [peak-40, peak+60];
// V-ratio = (max window in the following 80ms) / min. Flag V-ratio > 1.3.
// Also self-validates on a knowingly-dipping variant (band-scaled sag) --
// the metric must flag it or the gate is meaningless.
const fs=require('fs');
const FS=45000, PEAK=(1<<30)-(1<<15);
function build(path,patch){
  const html=fs.readFileSync(path,'utf8');
  let code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
  if(patch) code=patch(code);
  const mod={}; eval(code+'\nmod.render=render;');
  return mod.render;
}
function vdip(render,amount,seed){
  const p={attack:130,decay:256,peak:0.75,sustain:0.55,release:401,gate:575,
    amount,chiffDur:601,seed};
  const r=render(p);
  const win=[];
  for(let t=60;t+8<=280;t+=8){let s=0,n=0;
    for(let i=t*45;i<(t+8)*45;i++){const st=r.out[i]-r.out[i-1];s+=st*st;n++;}
    win.push({t,v:Math.sqrt(s/n)/PEAK*1e3});}
  const zone=win.filter(w=>w.t>=90&&w.t<=190);
  let min=zone[0]; for(const w of zone) if(w.v<min.v) min=w;
  const after=win.filter(w=>w.t>min.t&&w.t<=min.t+80);
  const rebound=after.length?Math.max(...after.map(w=>w.v)):0;
  return {minT:min.t, minV:min.v, ratio:min.v>0?rebound/min.v:0};
}
const target=process.argv[2];
const seeds=[0xCAFEBABE,0x722d7bb1,0xEB2A32A3];
// self-validation: the band-scaled-sag variant (known V per user's eyes)
const knownDip=code=>code.replace(
  "        const center = Math.min(base, maxLevel - tailF * dartSpan);",
  `        const sagAllow = Math.min(1, 3*Math.sqrt(alphaEff/(2*(2-alphaEff))))
           * runAmp / Math.SQRT2;
        const center = Math.max(base - sagAllow,
          Math.min(base, maxLevel - tailF * dartSpan));`);
for(const [label,render] of [
    ['NEW (guard-scaled)', build(target)],
    ['KNOWN-DIP (band-scaled sag)', build(target,knownDip)]]){
  let flagged=0;
  for(const amount of [42,96,127]){
    for(const seed of seeds){
      const d=vdip(render,amount,seed);
      if(d.ratio>1.3){flagged++;
        console.log(`  ${label} amt${amount} seed${seed.toString(16)}: V at ${d.minT}ms, rebound x${d.ratio.toFixed(2)}`);}
    }
  }
  console.log(`${label}: ${flagged}/9 flagged`);
}
