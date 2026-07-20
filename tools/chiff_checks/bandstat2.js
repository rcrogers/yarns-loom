// Banding metric replicating the DISPLAY: the sim's own STFT (512/64 Hann)
// on the residual (out - dialed render), band 5-20kHz, dB; a "band" is a
// column more than 6 dB below the local median (+-10 cols). Averaged over
// seeds. Args: <simA.html> <simB.html>
const fs=require('fs');
const FS=45000, PEAK=(1<<30)-(1<<15), N=512, HOP=64;
function load(path){
  const html=fs.readFileSync(path,'utf8');
  const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
  const fftSrc=html.slice(html.indexOf('function fft('),html.indexOf('function drawSpectrum'));
  const mod={}; eval(code+'\n'+fftSrc+'\nmod.render=render;mod.fft=fft;');
  return mod;
}
function stripes(mod,amount,seed){
  const p={attack:130,decay:256,peak:0.75,sustain:0.55,release:401,gate:575,
    amount,chiffDur:601,seed};
  const r=mod.render(p), d=mod.render(Object.assign({},p,{amount:0}));
  const b0=Math.floor(N*5000/FS), b1=Math.ceil(N*20000/FS);
  const re=new Float64Array(N), im=new Float64Array(N);
  const colDb=[];
  for(let s=60*45; s+N<=280*45; s+=HOP){
    for(let k=0;k<N;k++){const h=0.5-0.5*Math.cos(2*Math.PI*k/(N-1));
      re[k]=((r.out[s+k]-d.out[s+k])/PEAK)*h; im[k]=0;}
    mod.fft(re,im);
    let sum=0; for(let b=b0;b<b1;b++)sum+=Math.hypot(re[b],im[b]);
    colDb.push(20*Math.log10(sum/(b1-b0)+1e-12));
  }
  let stripes=0, worst=0;
  for(let c=10;c<colDb.length-10;c++){
    const win=colDb.slice(c-10,c+11).sort((a,b)=>a-b);
    const med=win[10];
    const depth=med-colDb[c];
    if(depth>6) stripes++;
    worst=Math.max(worst,depth);
  }
  return {stripes, worst};
}
const seeds=[]; let s=0xCAFEBABE;
for(let i=0;i<12;i++){ s^=(s<<13); s>>>=0; s^=(s>>>17); s^=(s<<5); s>>>=0; seeds.push(s); }
for(const [path,label] of [[process.argv[2],'A(approved)'],[process.argv[3],'B(current) ']]){
  const mod=load(path);
  for(const amount of [32,96]){
    let n=0,w=0;
    for(const seed of seeds){const x=stripes(mod,amount,seed);n+=x.stripes;w+=x.worst;}
    console.log(`${label} amt=${amount}: stripe-cols/render ${(n/seeds.length).toFixed(1)}, avg worst depth ${(w/seeds.length).toFixed(1)} dB`);
  }
}
