// Onset high-band energy vs chiff duration, using the sim's own STFT method.
// Bands: 15-21k (what the user showed), 5-10k. Windows 0-100ms.
const fs=require('fs');
const html=fs.readFileSync(process.argv[2],'utf8');
const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
const fftSrc=html.slice(html.indexOf('function fft('),html.indexOf('function drawSpectrum'));
const mod={}; eval(code+'\n'+fftSrc+'\nmod.render=render;mod.fft=fft;');
const FS=45000, PEAK=(1<<30)-(1<<15), N=512, HOP=64;
function bandEnergy(out,line,aMs,bMs,f0,f1){
  const re=new Float64Array(N), im=new Float64Array(N);
  const b0=Math.floor(N*f0/FS), b1=Math.ceil(N*f1/FS);
  let sum=0,cnt=0;
  for(let s=aMs*45; s+N<=bMs*45; s+=HOP){
    for(let k=0;k<N;k++){const h=0.5-0.5*Math.cos(2*Math.PI*k/(N-1));
      re[k]=((out[s+k]-line[s+k])/PEAK)*h; im[k]=0;}
    mod.fft(re,im);
    for(let b=b0;b<b1;b++)sum+=Math.hypot(re[b],im[b]);
    cnt+=(b1-b0);
  }
  return sum/cnt;
}
for(const amount of [64,96,127]){
  const rows=[];
  for(const dur of [300,1000,3000,8000]){
    const p={attack:1200,decay:1,peak:0.7,sustain:0.6,release:2000,gate:9000,
      amount,chiffDur:dur,seed:0xCAFEBABE};
    const r=mod.render(p), d=mod.render(Object.assign({},p,{amount:0}));
    const hi=bandEnergy(r.out,d.out,0,100,15000,21000);
    const mid=bandEnergy(r.out,d.out,0,100,5000,10000);
    rows.push(`${String(dur).padStart(4)}ms hi=${(20*Math.log10(hi/24+1e-9)).toFixed(1)}dB mid=${(20*Math.log10(mid/24+1e-9)).toFixed(1)}dB`);
  }
  console.log(`amt=${String(amount).padStart(3)}: `+rows.join('  '));
}
