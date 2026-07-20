// What is the visible "cliff" in the trace? Per-pixel max profile (like the
// sim's min/max decimation), vs the peak level (clamp) and sustain level.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
const ms=v=>Math.round(Math.exp((v/1000)*Math.log(8000)));

const p={attack:ms(789),decay:ms(0),peak:0.7,sustain:0.6,release:ms(667),
  gate:8000,amount:96,chiffDur:8000,seed:0xCAFEBABE};
const r=mod.render(p);
const peakLevel=Math.round(PEAK*p.peak), sus=Math.round(peakLevel*p.sustain);
console.log('peakLevel',(peakLevel/PEAK*100).toFixed(0)+'%','sustain',(sus/PEAK*100).toFixed(0)+'%');

const pw=1000;                       // pixel columns like the sim
const maxes=new Float64Array(pw+1);
for(let px=0;px<=pw;px++){
  const i0=Math.floor(px/pw*(r.totalN-1));
  const i1=Math.min(r.totalN-1,Math.floor((px+1)/pw*(r.totalN-1)));
  let hi=r.out[i0];
  for(let i=i0;i<=i1;i++)if(r.out[i]>hi)hi=r.out[i];
  maxes[px]=hi;
}
// histogram of per-pixel max as % of full scale, over the sustain span
const gatePx=Math.floor(r.gateN/(r.totalN-1)*pw);
const susStartPx=Math.floor((1201+400)/1000*45000/(r.totalN-1)*pw); // past attack+decay
const h={};
let railPx=0;
for(let px=susStartPx;px<gatePx;px++){
  const pct=Math.round(maxes[px]/PEAK*100);
  h[pct]=(h[pct]||0)+1;
  if(maxes[px]===peakLevel)railPx++;
}
console.log('per-pixel-max histogram over sustain span (%FS: pixel count):');
for(const k of Object.keys(h).sort((a,b)=>a-b))console.log(`  ${k}%: ${h[k]}`);
console.log('pixels whose max hits the rail exactly:',railPx,'of',gatePx-susStartPx);
let above=0; for(let px=0;px<=pw;px++)if(maxes[px]>peakLevel)above++;
console.log('pixels with max ABOVE peakLevel:',above);
