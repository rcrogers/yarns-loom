// Rail-pin fix prototype: clamp the dart TARGET to reachable headroom so the
// slew approaches the rail instead of slamming onto it. Compare vs baseline:
// (1) rail-pinned runs, (2) noise contour over time (should match except the
// pins), (3) overshoot, (4) mean sag near the rail.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const base=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const FIX=`dartTgt = (r & 0x10000) ? amp : -amp;
        const headUp = maxLevel - dialed, headDn = dialed;
        if (dartTgt > headUp) dartTgt = headUp;
        else if (dartTgt < -headDn) dartTgt = -headDn;`;
function build(src){const mod={};eval(src+'\nmod.render=render;');return mod.render;}
const renderBase=build(base);
const renderFix=build(base.replace('dartTgt = (r & 0x10000) ? amp : -amp;',FIX));
const FS=45000, PEAK=(1<<30)-(1<<15);
const ms=v=>Math.round(Math.exp((v/1000)*Math.log(8000)));

const p={attack:ms(789),decay:ms(0),peak:0.7,sustain:0.6,release:ms(667),
  gate:8000,amount:96,chiffDur:8000,seed:0xCAFEBABE};
const peakLevel=Math.round(PEAK*p.peak);

function analyze(render,label){
  const r=render(p);
  const d=render(Object.assign({},p,{amount:0}));
  let atRail=0,runs=0,run=0,longest=0,over=0;
  for(let i=0;i<r.totalN;i++){
    if(r.out[i]>peakLevel)over++;
    if(r.out[i]===peakLevel){atRail++;run++;longest=Math.max(longest,run);}
    else{if(run>0)runs++;run=0;}
  }
  console.log(`${label}: rail samples ${atRail} in ${runs} runs (longest ${(longest/45).toFixed(1)}ms), samples above peak ${over}`);
  const rows=[];
  for(const [a,b] of [[0,100],[100,300],[300,600],[600,900],[900,1200],[1200,1600],[1600,2400]]){
    let s=0,m=0,n=0;
    for(let i=Math.max(1,a*45);i<b*45;i++){s+=Math.abs(r.out[i]-r.out[i-1]);m+=r.out[i]-d.out[i];n++;}
    rows.push(`${a}-${b}ms noise ${(s/n/PEAK*1e3).toFixed(2)} bias ${(m/n/PEAK*100).toFixed(1)}%`);
  }
  console.log('  '+rows.join('\n  '));
}
analyze(renderBase,'BASELINE');
analyze(renderFix,'TARGET-CLAMP');
