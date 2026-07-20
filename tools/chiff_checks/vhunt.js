// V-dip hunter: grid-sweep the parameter space for band-brightness dips
// with recovery (the eye's complaint), display-faithful metric.
// Args: <sim.html> [--full]
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const fftSrc=html.slice(html.indexOf('function fft('),html.indexOf('function drawSpectrum'));
const mod={}; eval(code+'\n'+fftSrc+'\nmod.render=render;mod.fft=fft;');
const FS=45000, PEAK=(1<<30)-(1<<15), N=512, HOP=64, SPEC_REF=24;

function bandCols(p, upToMs){
  const r=mod.render(p), d=mod.render(Object.assign({},p,{amount:0}));
  const re=new Float64Array(N), im=new Float64Array(N);
  const cols=[];
  for(let s=0; s+N<=Math.min(upToMs*45, r.totalN); s+=HOP*5){
    for(let k=0;k<N;k++){const h=0.5-0.5*Math.cos(2*Math.PI*k/(N-1));
      re[k]=((r.out[s+k]-d.out[s+k])/PEAK)*h; im[k]=0;}
    mod.fft(re,im);
    let t=0,c=0;
    for(let b=N/4;b<N/2;b++){
      const db=20*Math.log10(Math.hypot(re[b],im[b])/SPEC_REF+1e-9);
      t+=Math.max(0,Math.min(1,(db+60)/60));c++;}
    cols.push({t:(s+N/2)/45, v:t/c});
  }
  return cols;
}
// V detector: min col with a later col rebounding >= REBOUND x the min,
// within a window after; min must also be meaningfully below earlier levels.
function findV(cols){
  let best=null;
  for(let i=2;i<cols.length-2;i++){
    const before=Math.max(...cols.slice(Math.max(0,i-6),i).map(c=>c.v));
    const after=Math.max(...cols.slice(i+1,Math.min(cols.length,i+8)).map(c=>c.v));
    const v=cols[i].v;
    if(v<0.03) continue;                      // ignore silence
    if(before>v*1.35 && after>v*1.35){        // genuine V: drop AND rebound
      const depth=Math.min(before,after)/v;
      if(!best || depth>best.depth) best={t:cols[i].t, depth};
    }
  }
  return best;
}
const seeds=[0xCAFEBABE, 0x722d7bb1];
let found=0, tested=0;
for(const amount of [16,42,96])
for(const sustain of [0.55,0.72,0.9])
for(const dur of [300,601,1280])
for(const attack of [60,130,260])
for(const seed of seeds){
  const p={attack,decay:256,peak:0.75,sustain,release:401,gate:575,
    amount,chiffDur:dur,seed};
  const v=findV(bandCols(p, 575));
  tested++;
  if(v){found++;
    console.log(`V: amt=${amount} sus=${sustain} dur=${dur} atk=${attack} seed=${seed.toString(16)} -> dip@${v.t.toFixed(0)}ms depth x${v.depth.toFixed(2)}`);}
}
console.log(`${found} V-dips in ${tested} cases`);
