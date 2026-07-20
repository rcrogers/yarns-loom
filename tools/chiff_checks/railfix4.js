// Near-rail dimming: compare (A) committed baseline (pins/dark lines),
// (B) current RAIL_CAP=8 asym cap (dim area at rail, rebrightens in decay),
// (C) symmetric shrink + BRIGHTNESS COMPENSATION: eff dart depth =
//     min(amp, 2*upRoom) both sides (zero-mean, reachable, no pins) and the
//     noise slew sped up by log2(amp/effAmp) shifts so high-band energy holds.
// Scenario ~ user's screenshot: 200ms attack, decay to 70% sustain, long chiff.
// Metric: per-window RMS of the first difference (high-band proxy) -- want it
// monotonic through attack end -> decay, no dip-then-rise; plus bias, pins.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
let src=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
// strip the working-tree RAIL_CAP patch to get a clean base, then re-patch per variant
const CAPPED=`        const upRoom = maxLevel - dialed;
        if (dartTgt > RAIL_CAP * upRoom) dartTgt = RAIL_CAP * upRoom;
`;
const hasCap=src.includes('RAIL_CAP * upRoom');
if(hasCap) src=src.replace(CAPPED,'');
const ORIG='dartTgt = (r & 0x10000) ? amp : -amp;           // RANDOM sign -> broadband, not a tone';
const SLEW='pert += (dartTgt - pert) * alphaOf(chiffShift);';
function build(dartPatch,slewPatch){const mod={};
  eval(src.replace(ORIG,dartPatch).replace(SLEW,slewPatch||SLEW)+'\nmod.render=render;');
  return mod.render;}
const FS=45000, PEAK=(1<<30)-(1<<15);
const p={attack:200,decay:300,peak:0.7,sustain:0.7,release:400,gate:1000,
  amount:96,chiffDur:600,seed:0xCAFEBABE};
const peakLevel=Math.round(PEAK*p.peak);

function analyze(render,label){
  const r=render(p), d=render(Object.assign({},p,{amount:0}));
  let atRail=0,run=0,longest=0;
  for(let i=0;i<r.totalN;i++){
    if(r.out[i]===peakLevel){atRail++;run++;longest=Math.max(longest,run);}else run=0;}
  const wins=[[0,50],[50,100],[100,150],[150,200],[200,250],[250,300],[300,400],[400,500]];
  const hi=[],bias=[];
  for(const [a,b] of wins){let s=0,m=0,n=0;
    for(let i=Math.max(1,a*45);i<b*45;i++){const st=r.out[i]-r.out[i-1];s+=st*st;m+=r.out[i]-d.out[i];n++;}
    hi.push(Math.sqrt(s/n)/PEAK*1e3);bias.push(m/n/PEAK*100);}
  console.log(`${label}: rail ${atRail} (longest ${(longest/45).toFixed(2)}ms)`);
  console.log('  hiRMS '+hi.map(x=>x.toFixed(1).padStart(6)).join(''));
  console.log('  bias  '+bias.map(x=>x.toFixed(1).padStart(6)).join(''));
}
console.log('windows(ms): 0-50 50-100 100-150 150-200 | 200-250 250-300 300-400 400-500 (attack ends 200)');
analyze(build(ORIG),'BASELINE (pins)   ');
analyze(build(ORIG+`
        const upRoom = maxLevel - dialed;
        if (dartTgt > 8 * upRoom) dartTgt = 8 * upRoom;`),'RAIL_CAP=8 (dim)  ');
analyze(build(
`const effAmp = Math.min(amp, 2 * (maxLevel - dialed));
        dartTgt = (r & 0x10000) ? effAmp : -effAmp;
        global._boost = Math.log2(amp / Math.max(effAmp, 1));`,
`pert += (dartTgt - pert) * alphaOf(Math.max(0, chiffShift - (global._boost||0)));`),
'SYM+BRIGHT-COMP   ');
