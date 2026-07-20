// Autopsy of a specific banded render: seed + defaults, current sim.
// Locate dark spectrogram columns (display-faithful: full 0-22.5k residual
// STFT, dB vs SPEC_REF=24) and print the process state inside them.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const seed=parseInt(process.argv[3],16)>>>0;
const AMT=parseInt(process.argv[4]||'96');
const DUR=parseInt(process.argv[5]||'601');
let code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
code=code.replace('runW = w;',
  'runW = w; if(global._st) global._st.push({i, w, up:runUp, dn:runDown, c:center, base});');
const fftSrc=html.slice(html.indexOf('function fft('),html.indexOf('function drawSpectrum'));
const mod={}; eval(code+'\n'+fftSrc+'\nmod.render=render;mod.fft=fft;');
const FS=45000, PEAK=(1<<30)-(1<<15), N=512, HOP=64, SPEC_REF=24;
const p={attack:130,decay:256,peak:0.75,sustain:0.55,release:401,gate:575,
  amount:AMT,chiffDur:DUR,seed};
global._st=[];
const r=mod.render(p);
const d=mod.render(Object.assign({},p,{amount:0}));
// full-band column dB exactly like drawSpectrogram (mean over bins, vs SPEC_REF)
const re=new Float64Array(N), im=new Float64Array(N);
const cols=[];
for(let s=0; s+N<=r.totalN; s+=HOP){
  for(let k=0;k<N;k++){const h=0.5-0.5*Math.cos(2*Math.PI*k/(N-1));
    re[k]=((r.out[s+k]-d.out[s+k])/PEAK)*h; im[k]=0;}
  mod.fft(re,im);
  let sum=0,cnt=0;
  for(let b=1;b<N/2;b++){sum+=Math.hypot(re[b],im[b]);cnt++;}
  cols.push({t:(s+N/2)/45, db:20*Math.log10(sum/cnt/SPEC_REF+1e-12)});
}
// dark columns: > 6 dB below +-10-col local median, within first 400ms
let found=0;
for(let c=10;c<cols.length-10 && cols[c].t<400;c++){
  const win=cols.slice(c-10,c+11).map(x=>x.db).sort((a,b)=>a-b);
  const depth=win[10]-cols[c].db;
  if(depth>5){
    found++;
    if(found<=6) console.log(`dark col @ ${cols[c].t.toFixed(1)}ms: ${cols[c].db.toFixed(1)} dB, ${depth.toFixed(1)} dB below local median`);
  }
}
console.log('total dark cols (<400ms):',found);
// print the deepest column's neighborhood + run states there
let minC=10;
for(let c=10;c<cols.length-10 && cols[c].t<400;c++) if(cols[c].db<cols[minC].db) minC=c;
console.log('deepest column @',cols[minC].t.toFixed(1),'ms:',cols[minC].db.toFixed(1),'dB; neighbors:',
  cols.slice(minC-3,minC+4).map(x=>x.db.toFixed(1)).join(' '));
const t0=parseFloat(process.argv[6]||cols[minC].t);
for(const s of global._st.filter(s=>Math.abs(s.i/45-t0)<3))
  console.log(`  run@${(s.i/45).toFixed(1)}ms w=${s.w.toFixed(3)} up=${(s.up/PEAK*100).toFixed(1)} dn=${(s.dn/PEAK*100).toFixed(1)} center=${(s.c/PEAK*100).toFixed(1)} base=${(s.base/PEAK*100).toFixed(1)}`);
// raw waveform around the deepest column
const a=Math.max(1,Math.floor((t0-2)*45)), b=Math.floor((t0+2)*45);
let mn=1e18,mx=-1e18,ac=0;
for(let i=a;i<b;i++){mn=Math.min(mn,r.out[i]);mx=Math.max(mx,r.out[i]);ac+=Math.abs(r.out[i]-r.out[i-1]);}
console.log(`waveform ${t0-2}..${t0+2}ms: range ${(mn/PEAK*100).toFixed(1)}..${(mx/PEAK*100).toFixed(1)}%, mean|step| ${(ac/(b-a)/PEAK*1e3).toFixed(2)}`);
