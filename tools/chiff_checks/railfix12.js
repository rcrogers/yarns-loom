// v4 bug: center shifts by RAW dart depth, but at low amounts the slewed noise
// only achieves a fraction of it -> line dips with no spikes near it.
// v4.1: shift by the realized reach: sigma = amp*sqrt(a/(2(2-a))) (AR(1) driven
// by +-amp half the time), reach = min(amp, 3*sigma).
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
let src=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const CUR='const center = Math.min(dialed, maxLevel - amp);  // shift down just enough to fit under the rail';
const FIX=`const a_ = alphaOf(chiffShift);
      const reach = Math.min(amp, 3 * amp * Math.sqrt(a_ / (2 * (2 - a_))));
      const center = Math.min(dialed, maxLevel - reach);`;
function build(patch){const mod={};eval(src.replace(CUR,patch)+'\nmod.render=render;');return mod.render;}
const FS=45000, PEAK=(1<<30)-(1<<15);
function analyze(render,amount,label){
  const p={attack:200,decay:300,peak:0.7,sustain:0.7,release:400,gate:1000,
    amount,chiffDur:600,seed:0xCAFEBABE};
  const r=render(p), d=render(Object.assign({},p,{amount:0}));
  const peakLevel=Math.round(PEAK*p.peak);
  // late-attack window 150-200ms: bias, spike top percentile, rail dwell
  let m=0,n=0,tops=[];
  for(let i=150*45;i<200*45;i++){m+=r.out[i]-d.out[i];n++;tops.push(r.out[i]);}
  tops.sort((x,y)=>y-x);
  const p999=tops[Math.floor(tops.length*0.001)];
  let dwell=0,run=0;
  for(let i=0;i<r.totalN;i++){if(r.out[i]===peakLevel){run++;dwell=Math.max(dwell,run);}else run=0;}
  console.log(`  amt=${String(amount).padStart(3)}: bias ${(m/n/PEAK*100).toFixed(1).padStart(6)}%  spikeTop99.9 ${(p999/PEAK*100).toFixed(1)}% (rail 70%)  dwell ${(dwell/45).toFixed(2)}ms`);
}
for(const [patch,name] of [[CUR,'v4 (current)'],[FIX,'v4.1 reach  ']]){
  console.log(name);
  const render=build(patch);
  for(const amount of [5,10,20,40,96,127]) analyze(render,amount,name);
}
