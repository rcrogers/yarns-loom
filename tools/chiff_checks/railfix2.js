// Scoped rail-pin fixes, swept. Variants:
//  asym c: up-dart target capped at c*headroom (down full) -> engages only when
//          amp > c*headroom (near the rail); output clamp still trims.
//  sym c:  BOTH dart amplitudes shrink to min(amp, c*headroom) -> zero-mean,
//          no trims, but dims the down-side too.
// Score vs baseline: windowed noise contour, darkest 1.4ms hop in the late
// attack (dark-line depth), level bias, rail dwell.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const base=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const ORIG='dartTgt = (r & 0x10000) ? amp : -amp;';
function build(patch){const mod={};eval(base.replace(ORIG,patch)+'\nmod.render=render;');return mod.render;}
const FS=45000, PEAK=(1<<30)-(1<<15);
const ms=v=>Math.round(Math.exp((v/1000)*Math.log(8000)));
const p={attack:ms(789),decay:ms(0),peak:0.7,sustain:0.6,release:ms(667),
  gate:8000,amount:96,chiffDur:8000,seed:0xCAFEBABE};
const peakLevel=Math.round(PEAK*p.peak);

function analyze(render,label){
  const r=render(p), d=render(Object.assign({},p,{amount:0}));
  let atRail=0,run=0,longest=0;
  for(let i=0;i<r.totalN;i++){
    if(r.out[i]===peakLevel){atRail++;run++;longest=Math.max(longest,run);}else run=0;}
  // windowed noise contour + bias
  const wins=[[0,100],[100,300],[300,600],[600,900],[900,1200],[1200,1600],[1600,2400]];
  const noise=[],bias=[];
  for(const [a,b] of wins){let s=0,m=0,n=0;
    for(let i=Math.max(1,a*45);i<b*45;i++){s+=Math.abs(r.out[i]-r.out[i-1]);m+=r.out[i]-d.out[i];n++;}
    noise.push(s/n/PEAK*1e3);bias.push(m/n/PEAK*100);}
  // darkest 64-sample hop in 600-1200ms (late attack), as % of that region's mean
  let hopMin=1e18,hopSum=0,hopN=0;
  for(let h=600*45;h+64<=1200*45;h+=64){let s=0;
    for(let i=h;i<h+64;i++)s+=Math.abs(r.out[i]-r.out[i-1]);
    s/=64;hopMin=Math.min(hopMin,s);hopSum+=s;hopN++;}
  const hopMean=hopSum/hopN;
  console.log(`${label}: rail ${atRail} (longest ${(longest/45).toFixed(2)}ms) darkestHop ${(hopMin/hopMean*100).toFixed(0)}% of regional mean`);
  console.log('  noise '+noise.map(x=>x.toFixed(1)).join(' | '));
  console.log('  bias  '+bias.map(x=>x.toFixed(1)).join(' | '));
}
analyze(build(ORIG),'BASELINE       ');
for(const c of [2,3,4,6]){
  analyze(build(`dartTgt = (r & 0x10000) ? amp : -amp;
    const headUp = maxLevel - dialed;
    if (dartTgt > ${c} * headUp) dartTgt = ${c} * headUp;`),`asym c=${c}      `);
}
for(const c of [2,3,4,6]){
  analyze(build(`const effAmp = Math.min(amp, ${c} * (maxLevel - dialed));
        dartTgt = (r & 0x10000) ? effAmp : -effAmp;`),`sym  c=${c}      `);
}
for(const c of [2,3,4,6]){
  analyze(build(`const headUp = maxLevel - dialed;
        const u = Math.min(amp, ${c} * headUp);
        // rebalance: up-darts capped near the rail but more frequent, down rarer
        // but full-depth -> average stays 0 (telegraph statistics at the rail)
        const pUp = amp / (u + amp);
        dartTgt = (((r >>> 17) & 0x7FFF) / 32768 < pUp) ? u : -amp;`),`bal  c=${c}      `);
}
