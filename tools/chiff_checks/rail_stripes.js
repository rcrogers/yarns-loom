// Attack-near-target: are the spectrogram's vertical dark lines runs of samples
// pinned at the top rail (residual momentarily zero)? And does out ever exceed
// the peak level (true overshoot) or just reach it early (darts to the rail
// before dialed gets there)?
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const mod={}; eval(code+'\nmod.render=render;');
const FS=45000, PEAK=(1<<30)-(1<<15);
const ms=v=>Math.round(Math.exp((v/1000)*Math.log(8000)));

const p={attack:ms(789),decay:ms(0),peak:0.7,sustain:0.6,release:ms(667),
  gate:8000,amount:96,chiffDur:8000,seed:0xCAFEBABE};
const r=mod.render(p);
const peakLevel=Math.round(PEAK*p.peak);
let overMax=0, atRail=0, railRuns=[], run=0;
for(let i=0;i<r.totalN;i++){
  if(r.out[i]>peakLevel) overMax=Math.max(overMax,r.out[i]-peakLevel);
  if(r.out[i]===peakLevel){atRail++;run++;}
  else{if(run>0)railRuns.push([i-run,run]);run=0;}
}
console.log('samples above peakLevel:',overMax?overMax:'none');
console.log('samples exactly at rail:',atRail,'in',railRuns.length,'runs');
// where do rail runs live, and how long (a SPEC_HOP=64 ~1.4ms hop -> runs of
// tens of samples already dim a spectrogram column)?
const byMs={};
for(const [start,len] of railRuns){const t=Math.floor(start/45/100)*100;
  byMs[t]=(byMs[t]||{n:0,max:0});byMs[t].n++;byMs[t].max=Math.max(byMs[t].max,len);}
for(const t of Object.keys(byMs).sort((a,b)=>a-b))
  console.log(`  ${String(t).padStart(5)}ms: ${byMs[t].n} runs, longest ${byMs[t].max} samples (${(byMs[t].max/45).toFixed(1)}ms)`);
// first time out reaches the rail vs when dialed does (the "darts past" look)
const d=mod.render(Object.assign({},p,{amount:0}));
let firstOut=-1, firstDialed=-1;
for(let i=0;i<r.totalN;i++){
  if(firstOut<0&&r.out[i]>=peakLevel)firstOut=i;
  if(firstDialed<0&&d.out[i]>=0.98*peakLevel)firstDialed=i;}
console.log('out first touches rail at',(firstOut/45).toFixed(0),'ms; dialed reaches 98% at',(firstDialed/45).toFixed(0),'ms');
