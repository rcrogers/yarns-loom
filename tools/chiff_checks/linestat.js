// Multi-seed hi-band line count, image-space (the metric that finally sees
// what the eye sees). Args: <simA.html> <simB.html> [amount] [durMs]
const fs=require('fs');
const FS=45000, PEAK=(1<<30)-(1<<15), N=512, HOP=64, SPEC_REF=24;
const AMT=parseInt(process.argv[4]||'30'), DUR=parseInt(process.argv[5]||'1280');
function load(path){
  const html=fs.readFileSync(path,'utf8');
  const code=html.slice(html.indexOf('const FS = 45000'),html.indexOf('function dialed('));
  const fftSrc=html.slice(html.indexOf('function fft('),html.indexOf('function drawSpectrum'));
  const mod={}; eval(code+'\n'+fftSrc+'\nmod.render=render;mod.fft=fft;');
  return mod;
}
function lines(mod,seed){
  const p={attack:130,decay:256,peak:0.75,sustain:0.55,release:401,gate:575,
    amount:AMT,chiffDur:DUR,seed};
  const r=mod.render(p), d=mod.render(Object.assign({},p,{amount:0}));
  const re=new Float64Array(N), im=new Float64Array(N);
  // per-column mean display brightness over the UPPER half of bands
  const prof=[];
  for(let s=0; s+N<=Math.min(r.totalN, 430*45); s+=HOP){
    for(let k=0;k<N;k++){const h=0.5-0.5*Math.cos(2*Math.PI*k/(N-1));
      re[k]=((r.out[s+k]-d.out[s+k])/PEAK)*h; im[k]=0;}
    mod.fft(re,im);
    let t=0,cnt=0;
    for(let b=N/4;b<N/2;b++){
      const db=20*Math.log10(Math.hypot(re[b],im[b])/SPEC_REF+1e-9);
      t+=Math.max(0,Math.min(1,(db+60)/60));cnt++;}
    prof.push(t/cnt*255);
  }
  let n=0;
  for(let c=8;c<prof.length-8;c++){
    const win=prof.slice(c-8,c+9).slice().sort((a,b)=>a-b);
    const med=win[8];
    if(med>4 && prof[c]<0.65*med){ n++; if(process.env.SHOW) console.log('    line @ '+((c*64+256)/45).toFixed(1)+'ms depth '+(prof[c]/med*100).toFixed(0)+'%'); }
  }
  return n;
}
const seeds=[]; let s=0xEB2A32A3;
for(let i=0;i<10;i++){ s^=(s<<13); s>>>=0; s^=(s>>>17); s^=(s<<5); s>>>=0; seeds.push(s); }
seeds[0]=0xEB2A32A3;
for(const [path,label] of [[process.argv[2],'A'],[process.argv[3],'B']]){
  const mod=load(path);
  const counts=seeds.map(seed=>lines(mod,seed));
  console.log(`${label}: line-cols per render: [${counts.join(',')}] avg ${(counts.reduce((a,b)=>a+b)/counts.length).toFixed(1)}`);
}
